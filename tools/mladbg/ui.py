"""Terminal styling and lazy variable trees, independent of LLDB's ABI."""
from collections import OrderedDict
from dataclasses import dataclass, field
import re
import os
import shlex
import time


class FileCompletion:
    """Bounded directory browsing, without recursively walking project trees."""
    def __init__(self, command, cwd=None):
        self.cwd = cwd or os.getcwd()
        self.prefix = ""
        self.fragment = ""
        self.suffix = ""
        self.choices = []
        self.cursor = 0
        self.message = ""
        self.breakpoint = False
        match = re.fullmatch(r"(b|break|file|target\s+create|command\s+source)(?:\s+(.*))?", command)
        if not match:
            self.message = "File completion: b, break, file, target create, command source"
            return
        self.prefix = match[1]+" "
        self.breakpoint = match[1] in ("b", "break")
        self.fragment = (match[2] or "").strip().strip("\"'")
        if self.breakpoint:
            line = re.search(r":\d*$", self.fragment)
            if line:
                self.fragment, self.suffix = self.fragment[:line.start()], line.group()
                self.fragment = self.fragment.strip("\"'")
        self.refresh()

    def refresh(self):
        self.cursor = 0
        self.choices = []
        directory, partial = os.path.split(self.fragment)
        base = os.path.expanduser(directory or ".")
        if not os.path.isabs(base):
            base = os.path.join(self.cwd, base)
        self.message = "No matching files"
        deadline = time.monotonic()+0.1
        limited = False
        try:
            with os.scandir(base) as entries:
                for i, entry in enumerate(entries):
                    if i >= 4096 or time.monotonic() > deadline:
                        limited = True
                        break
                    if not entry.name.startswith(partial):
                        continue
                    try:
                        directory_entry = entry.is_dir(follow_symlinks=True)
                    except OSError:
                        directory_entry = False
                    path = os.path.join(directory, entry.name)
                    self.choices.append((path+"/" if directory_entry else path, directory_entry))
            self.choices.sort(key=lambda entry: (not entry[1], entry[0].casefold(), entry[0]))
            if len(self.choices) > 256:
                limited = True
                self.choices = self.choices[:256]
            if self.choices:
                self.message = "%d matches%s" % (len(self.choices), " (limited; type a longer prefix)" if limited else "")
            elif limited:
                self.message = "No matches in bounded scan (listing incomplete)"
        except OSError as error:
            self.message = "Cannot browse: " + str(error)

    def move(self, delta):
        self.cursor = max(0, min(max(0, len(self.choices)-1), self.cursor+delta))

    def accept(self):
        """Return a completed command; directories instead reopen their children."""
        if not self.choices:
            return None
        path, directory = self.choices[self.cursor]
        if directory:
            self.fragment = path
            self.refresh()
            return None
        path += self.suffix
        path = os.path.expanduser(path)
        # b treats its entire argument as a path; other LLDB commands tokenize.
        return self.prefix+(path if self.breakpoint else shlex.quote(path))

    def parent(self):
        directory = os.path.dirname(self.fragment)
        if directory.startswith("~"):
            directory = os.path.expanduser(directory)
        parent = os.path.normpath(os.path.join(directory or ".", ".."))
        self.fragment = "" if parent == "." else parent.rstrip("/")+"/"
        self.refresh()


KEYWORDS = set("fn let var struct enum impl trait pub mod use return if else for while loop "
               "break continue match switch case default unsafe generic cexpr extern in "
               "as where self Self const static defer throw try catch async await move".split())
TYPES = set("void bool bit i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 str8 str16 ptr list map "
            "tuple array multiarray mutmultiarray int long unsigned signed char double float".split())
TOKENS = re.compile(r'//.*|/\*.*?(?:\*/|$)|(?:u)?"(?:\\.|[^"\\])*"|'
                    r"'(?:\\.|[^'\\])*'|<unavailable[^>]*>|<invalid[^>]*>|"
                    r'0x[\da-fA-F]+|\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b|[A-Za-z_]\w*')


def token_spans(text):
    """Classify display tokens; quoted strings/comments never become keywords."""
    for match in TOKENS.finditer(text):
        token = match.group()
        if token.startswith(("//", "/*")):
            role = "comment"
        elif token.startswith(('"', 'u"', "'")):
            role = "string"
        elif token.startswith(("<unavailable", "<invalid")):
            role = "error"
        elif token in ("true", "false", "null", "nullptr") or token[0].isdigit():
            role = "number"
        elif token in KEYWORDS:
            role = "keyword"
        elif token in TYPES or token[0].isupper():
            role = "type"
        else:
            continue
        yield match.start(), match.end(), role


class Theme:
    def __init__(self, curses, enabled=True):
        self.curses = curses
        self.enabled = False
        self.pairs = {}
        if not enabled or not curses.has_colors():
            return
        try:
            curses.start_color()
            background = -1
            try:
                curses.use_default_colors()
            except curses.error:
                background = curses.COLOR_BLACK
            default_foreground = -1 if background == -1 else curses.COLOR_WHITE
            roles = {"text": default_foreground, "border": curses.COLOR_CYAN,
                     "keyword": curses.COLOR_MAGENTA, "type": curses.COLOR_CYAN,
                     "string": curses.COLOR_GREEN, "number": curses.COLOR_YELLOW,
                     "comment": default_foreground, "error": curses.COLOR_RED}
            for selected in (False, True):
                for role, foreground in roles.items():
                    pair = len(self.pairs)+1
                    # Bright yellow keywords remain legible on the blue row.
                    if selected and role == "keyword":
                        foreground = curses.COLOR_YELLOW
                    elif selected and foreground == -1:
                        foreground = curses.COLOR_WHITE
                    curses.init_pair(pair, foreground, curses.COLOR_BLUE if selected else background)
                    self.pairs[role, selected] = pair
            self.enabled = True
        except curses.error:
            self.pairs.clear()

    def attr(self, role="text", selected=False):
        if self.enabled:
            attr = self.curses.color_pair(self.pairs[role, selected])
        else:
            attr = self.curses.A_REVERSE if selected else 0
        if role in ("keyword", "type") or selected:
            attr |= self.curses.A_BOLD
        if role == "comment" and not selected:
            attr |= self.curses.A_DIM
        return attr


@dataclass
class TreeState:
    expanded: set = field(default_factory=set)
    saved: object = None
    selected: tuple = ()


@dataclass
class TreeRow:
    path: tuple
    parent: tuple
    depth: int
    text: str
    expandable: bool = False


class VariableTree:
    """Rebuild visible rows only; collapsed objects never read child values."""
    def __init__(self):
        self.states = OrderedDict()
        self.state = TreeState()
        self.rows = []
        self.cursor = 0

    def refresh(self, frame, context):
        if context not in self.states:
            if len(self.states) >= 64:
                self.states.popitem(last=False)
            self.states[context] = TreeState()
        self.states.move_to_end(context)
        self.state = self.states[context]
        self.rows = []
        occurrences = {}
        for value in frame.GetVariables(True, True, False, True):
            name = value.GetName() or "value"
            occurrence = occurrences.get(name, 0)
            occurrences[name] = occurrence+1
            self._visit(value, ((name, occurrence),), (), 0, name)
        self.cursor = next((i for i, row in enumerate(self.rows)
                            if row.path == self.state.selected), 0)
        if self.rows:
            self.state.selected = self.rows[self.cursor].path
        return [row.text for row in self.rows]

    def _visit(self, value, path, parent, depth, name):
        if len(self.rows) >= 256:
            return
        type_ = value.GetType()
        type_name = type_.GetName() or "unknown"
        prefix = "  " * depth
        header = "(%s) %s" % (type_name, name)
        error = value.GetError()
        if error.Fail():
            self.rows.append(TreeRow(path, parent, depth, prefix+"    "+header+
                                     " = <unavailable: %s>" % error.GetCString()))
            return
        pointer = type_.IsPointerType()
        summary = value.GetSummary() if pointer or value.GetNumChildren() == 0 else None
        scalar = value.GetValue()
        if type_name in ("unsigned char", "u8"):
            scalar, summary = str(value.GetValueAsUnsigned()), None
        elif type_name in ("signed char", "i8"):
            scalar, summary = str(value.GetValueAsSigned()), None
        is_string = summary and ('"' in summary)
        count = value.GetNumChildren() if not pointer else 0
        expandable = count > 0
        details = "{%d fields}" % count if expandable else "= " + (summary or scalar or "<unavailable>")
        collection = type_name.startswith(("list<", "array<", "multiarray<", "mutmultiarray<", "map<"))
        length = value.GetChildMemberWithName("len") if collection else None
        data = value.GetChildMemberWithName("data") if collection else None
        keys = value.GetChildMemberWithName("keys") if collection else None
        values = value.GetChildMemberWithName("values") if collection else None
        if length and length.IsValid():
            count = length.GetValueAsSigned()
            if length.GetError().Fail() or count < 0:
                expandable, details = False, "= <invalid or unavailable length>"
            else:
                expandable, details = count > 0, "len=%d" % count
        elif pointer and not is_string:
            pointee = type_.GetPointeeType()
            expandable = bool(value.GetValueAsUnsigned() and pointee.IsValid() and pointee.GetByteSize())
        if depth >= 8:
            expandable = False
            if count:
                details += " (depth limit)"
        expanded = expandable and path in self.state.expanded
        marker = "[-] " if expanded else "[+] " if expandable else "    "
        self.rows.append(TreeRow(path, parent, depth, prefix+marker+header+" "+details, expandable))
        if not expanded:
            return
        if pointer:
            self._visit(value.Dereference(), path+("*",), path, depth+1, "*"+name)
        elif collection and length and length.IsValid():
            for i in range(min(count, 16)):
                for label, buffer in (("[%d]" % i, data),) if data and data.IsValid() else (
                        ("[%d].key" % i, keys), ("[%d].value" % i, values)):
                    child_path = path+(label,)
                    if not buffer or not buffer.IsValid() or buffer.GetError().Fail():
                        self._unavailable(child_path, path, depth+1, label)
                        continue
                    element_type = buffer.GetType().GetPointeeType()
                    address, size = buffer.GetValueAsUnsigned(), element_type.GetByteSize()
                    if not address or not size:
                        self._unavailable(child_path, path, depth+1, label)
                        continue
                    child = buffer.CreateValueFromAddress(label, address+i*size, element_type)
                    self._visit(child, child_path, path, depth+1, label)
        else:
            for i in range(min(count, 16)):
                child = value.GetChildAtIndex(i)
                self._visit(child, path+(i,), path, depth+1, child.GetName() or "[%d]" % i)
        if count > 16 and not pointer and len(self.rows) < 256:
            self.rows.append(TreeRow(path+("more",), path, depth+1,
                                     "  "*(depth+1)+"... %d more (use p to inspect)" % (count-16)))

    def _unavailable(self, path, parent, depth, label):
        if len(self.rows) < 256:
            self.rows.append(TreeRow(path, parent, depth,
                                     "  "*depth+label+" = <unavailable data>"))

    def move(self, delta):
        if self.rows:
            self.cursor = max(0, min(len(self.rows)-1, self.cursor+delta))
            self.state.selected = self.rows[self.cursor].path

    def expand(self):
        if self.rows and self.rows[self.cursor].expandable:
            path = self.rows[self.cursor].path
            if path in self.state.expanded and self.cursor+1 < len(self.rows) and self.rows[self.cursor+1].parent == path:
                self.move(1)
            else:
                self.state.expanded.add(path)

    def collapse(self):
        if not self.rows:
            return False
        row = self.rows[self.cursor]
        if row.path in self.state.expanded:
            self.state.expanded.remove(row.path)
            return True
        elif row.parent:
            self.state.expanded.discard(row.parent)
            self.state.selected = row.parent
            return True
        return False

    def collapse_all(self):
        if self.state.saved is None:
            self.state.saved = (set(self.state.expanded), self.state.selected)
        self.state.expanded.clear()
        if self.rows:
            self.state.selected = self.rows[self.cursor].path[:1]

    def restore(self):
        if self.state.saved is not None:
            expanded, selected = self.state.saved
            self.state.expanded, self.state.selected = expanded, selected
            self.state.saved = None
