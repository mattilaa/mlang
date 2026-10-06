"""Check lazy tree browsing and terminal-independent syntax/color behavior."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace
import tempfile

sys.path.insert(0, str(Path(__file__).parents[1] / "tools" / "mladbg"))
from ui import Theme, Glyphs, VariableTree, FileCompletion, token_spans
from mladbg import assemble_instruction


class GlyphTests(unittest.TestCase):
    def test_unicode_boxes_and_ascii_fallback(self):
        glyphs = Glyphs(encoding="UTF-8")
        self.assertTrue(glyphs.enabled)
        self.assertEqual(glyphs.top_left+glyphs.horizontal+glyphs.top_right, "┌─┐")
        self.assertEqual(glyphs.bottom_left+glyphs.vertical+glyphs.bottom_right, "└│┘")
        self.assertEqual(glyphs.text("café"), "café")
        for glyphs in (Glyphs(False), Glyphs(encoding="ascii"), Glyphs(encoding="ISO-8859-1")):
            self.assertFalse(glyphs.enabled)
            self.assertEqual(glyphs.top_left+glyphs.horizontal+glyphs.top_right, "+-+")
            self.assertEqual(glyphs.bottom_left+glyphs.vertical+glyphs.bottom_right, "+|+")
            self.assertEqual(glyphs.text("mladbg — café"), "mladbg - caf\\xe9")


class AssemblyTests(unittest.TestCase):
    def test_nop_fills_instruction(self):
        self.assertEqual(assemble_instruction("nop", "x86_64-unknown-linux", 5), b"\x90"*5)
        self.assertEqual(assemble_instruction("nop", "arm64-apple-macosx", 4), bytes.fromhex("1f2003d5"))

    def test_rejects_directives_labels_and_newlines(self):
        for text in (".byte 0", "label: nop", "nop\n.byte 0", "nop; .org 8"):
            with self.assertRaises(ValueError):
                assemble_instruction(text, "x86_64-unknown-linux", 1)

    @patch("mladbg.shutil.which", return_value="clang")
    @patch("mladbg.subprocess.run")
    def test_rejects_symbolic_fixups(self, run, which):
        run.return_value = SimpleNamespace(returncode=0, stdout="jmp foo # encoding: [0xe9,A,A,A,A]", stderr="")
        with self.assertRaisesRegex(ValueError, "relocations"):
            assemble_instruction("jmp foo", "x86_64-unknown-linux", 5)
        run.return_value.stdout = "mov # encoding: [0x20,0x01,0x80,0xd2]"
        self.assertEqual(assemble_instruction("mov x0, #9", "arm64-apple-macosx", 4),
                         bytes.fromhex("200180d2"))


class CompletionTests(unittest.TestCase):
    def test_prefix_browsing_and_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/"modules").mkdir()
            (root/"modules"/"main.txt").touch()
            (root/"main.txt").touch()
            (root/"other.txt").touch()
            completion = FileCompletion("b m", directory)
            self.assertEqual(completion.choices, [("modules/", True), ("main.txt", False)])
            completion.move(1)
            self.assertEqual(completion.accept(), "b main.txt")
            completion.move(-1)
            self.assertIsNone(completion.accept())
            self.assertEqual(completion.accept(), "b modules/main.txt")
            completion.parent()
            self.assertEqual(completion.fragment, "")
            self.assertEqual(len(completion.choices), 3)
            self.assertEqual(FileCompletion("b", directory).choices, completion.choices)

    def test_spaces_line_suffix_and_empty_results(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/"my file.txt").touch()
            completion = FileCompletion('break "my":12', directory)
            self.assertEqual(completion.accept(), "break my file.txt:12")
            self.assertEqual(FileCompletion("file my", directory).accept(), "file 'my file.txt'")
            empty = FileCompletion("b missing/", directory)
            empty.move(100)
            self.assertIsNone(empty.accept())
            self.assertIn("Cannot browse", empty.message)
            self.assertIsNone(FileCompletion("p count", directory).accept())

    def test_listing_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            for i in range(300):
                (Path(directory)/("item%03d.txt" % i)).touch()
            completion = FileCompletion("b", directory)
            self.assertEqual(len(completion.choices), 256)
            self.assertIn("limited", completion.message)

    def test_empty_directory_and_navigation_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/"empty").mkdir()
            completion = FileCompletion("b em", directory)
            completion.move(100)
            self.assertEqual(completion.cursor, 0)
            self.assertIsNone(completion.accept())
            self.assertEqual(completion.fragment, "empty/")
            self.assertIn("No matching files", completion.message)
            completion.move(-100)
            self.assertEqual(completion.cursor, 0)
            completion.parent()
            self.assertEqual(completion.choices, [("empty/", True)])


class FakeType:
    def __init__(self, name, size=8, pointee=None):
        self.name, self.size, self.pointee = name, size, pointee

    def GetName(self): return self.name
    def IsPointerType(self): return self.pointee is not None
    def GetPointeeType(self): return self.pointee or FakeType("void", 0)
    def GetByteSize(self): return self.size
    def IsValid(self): return self.size > 0


class FakeError:
    def __init__(self, failed=False): self.failed = failed
    def Fail(self): return self.failed
    def GetCString(self): return "unreadable memory"


class FakeValue:
    def __init__(self, name, type_name="i32", scalar=None, children=(), type_=None):
        self.name, self.type = name, type_ or FakeType(type_name)
        self.scalar, self.children = scalar, list(children)
        self.reads = 0
        self.memory_reads = []
        self.memory = []

    def GetName(self): return self.name
    def GetType(self): return self.type
    def GetError(self): return FakeError()
    def IsValid(self): return True
    def GetSummary(self): return None
    def GetValue(self): return str(self.scalar) if self.scalar is not None else None
    def GetValueAsSigned(self): return int(self.scalar or 0)
    def GetValueAsUnsigned(self): return int(self.scalar or 0)
    def GetNumChildren(self): return len(self.children)
    def GetChildMemberWithName(self, name):
        return next((child for child in self.children if child.name == name), None)
    def GetChildAtIndex(self, index):
        self.reads += 1
        return self.children[index]
    def CreateValueFromAddress(self, name, address, type_):
        self.memory_reads.append(address)
        index = (address-self.scalar)//type_.size
        return self.memory[index]


class FakeFrame:
    def __init__(self, *values): self.values = values
    def GetVariables(self, *args): return self.values


class FakeCurses:
    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW = range(4)
    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE = range(4, 8)
    A_BOLD, A_DIM, A_REVERSE = 1 << 20, 1 << 21, 1 << 22
    error = RuntimeError
    def __init__(self, supported=True, fail=False):
        self.supported, self.fail, self.pairs = supported, fail, {}
    def has_colors(self): return self.supported
    def start_color(self): pass
    def use_default_colors(self): pass
    def init_pair(self, pair, foreground, background):
        if self.fail: raise self.error("limited palette")
        self.pairs[pair] = foreground, background
    def color_pair(self, pair): return pair << 8


class UITest(unittest.TestCase):
    def test_collapsed_structs_do_not_fetch_children(self):
        position = FakeValue("position", "Position", children=[FakeValue("x", scalar=3)])
        team = FakeValue("team", "Team", children=[position, FakeValue("count", scalar=5)])
        frame, tree = FakeFrame(team), VariableTree()
        lines = tree.refresh(frame, "review")
        self.assertEqual(len(lines), 1)
        self.assertIn("[+]", lines[0])
        self.assertEqual((team.reads, position.reads), (0, 0))
        tree.expand()
        tree.refresh(frame, "review")
        self.assertEqual(position.reads, 0)
        tree.move(1)
        tree.expand()
        lines = tree.refresh(frame, "review")
        self.assertTrue(any("x = 3" in line for line in lines))
        selection, expanded = tree.state.selected, set(tree.state.expanded)
        tree.collapse_all()
        tree.collapse_all()  # Repeating J must not replace the saved state.
        self.assertEqual(len(tree.refresh(frame, "review")), 1)
        tree.restore()
        tree.refresh(frame, "review")
        self.assertEqual(tree.state.expanded, expanded)
        self.assertEqual(tree.state.selected, selection)
        tree.collapse()
        lines = tree.refresh(frame, "review")
        self.assertFalse(any("x = 3" in line for line in lines))
        tree.collapse()  # On a collapsed child, go back to its parent.
        self.assertEqual(len(tree.refresh(frame, "review")), 1)

    def test_frame_state_and_values_refresh(self):
        scalar = FakeValue("count", scalar=7)
        box = FakeValue("box", "Box", children=[scalar])
        tree, frame = VariableTree(), FakeFrame(box)
        tree.refresh(frame, "caller")
        tree.expand()
        tree.refresh(FakeFrame(FakeValue("other", scalar=4)), "callee")
        self.assertFalse(tree.state.expanded)
        scalar.scalar = 12
        self.assertTrue(any("count = 12" in line for line in tree.refresh(frame, "caller")))
        self.assertTrue(tree.state.expanded)

    def test_moving_up_and_down_preserves_expansions(self):
        box = FakeValue("box", "Box", children=[FakeValue("value", scalar=2)])
        frame = FakeFrame(FakeValue("first", scalar=1), box, FakeValue("last", scalar=3))
        tree = VariableTree()
        tree.refresh(frame, "main")
        tree.move(2)
        tree.move(-1)
        tree.refresh(frame, "main")
        self.assertEqual(tree.state.selected[0][0], "box")
        tree.expand()
        tree.refresh(frame, "main")
        expanded = set(tree.state.expanded)
        tree.move(100)  # Traverse the whole expanded tree to the final scalar.
        for _ in range(10):
            tree.move(-1)
            lines = tree.refresh(frame, "main")
            self.assertEqual(tree.state.expanded, expanded)
            self.assertTrue(any("value = 2" in line for line in lines))
        self.assertEqual(tree.cursor, 0)
        for _ in range(10):
            tree.move(1)
            tree.refresh(frame, "main")
            self.assertEqual(tree.state.expanded, expanded)
        self.assertEqual(tree.cursor, len(tree.rows) - 1)

    def test_collection_reads_are_lazy_and_bounded(self):
        length = FakeValue("len", "i64", scalar=1000000)
        data = FakeValue("data", scalar=4096, type_=FakeType("i32 *", pointee=FakeType("i32", 4)))
        data.memory = [FakeValue("[%d]" % i, scalar=i) for i in range(16)]
        values = FakeValue("values", "list<i32>", children=[length, data])
        tree, frame = VariableTree(), FakeFrame(values)
        tree.refresh(frame, "main")
        self.assertFalse(data.memory_reads)
        tree.expand()
        lines = tree.refresh(frame, "main")
        self.assertEqual(len(data.memory_reads), 16)
        self.assertTrue(any("999984 more" in line for line in lines))
        data.scalar = 0
        self.assertTrue(any("<unavailable data>" in line for line in tree.refresh(frame, "main")))
        length.scalar = -1
        self.assertEqual(len(tree.refresh(frame, "main")), 1)

    def test_keyword_spans_respect_strings_and_comments(self):
        text = 'let x: i32 = 42; println!("return false"); // fn true'
        spans = [(text[a:b], role) for a, b, role in token_spans(text)]
        self.assertIn(("let", "keyword"), spans)
        self.assertIn(("i32", "type"), spans)
        self.assertIn(("42", "number"), spans)
        self.assertIn(('"return false"', "string"), spans)
        self.assertIn(("// fn true", "comment"), spans)
        self.assertNotIn(("return", "keyword"), spans)

    def test_colors_disabled_or_unavailable(self):
        for curses, enabled in ((FakeCurses(), False), (FakeCurses(supported=False), True),
                                (FakeCurses(fail=True), True)):
            theme = Theme(curses, enabled=enabled)
            self.assertFalse(theme.enabled)
            self.assertTrue(theme.attr(selected=True) & curses.A_REVERSE)
        curses = FakeCurses()
        theme = Theme(curses)
        self.assertTrue(theme.enabled)
        for (role, selected), pair in theme.pairs.items():
            if selected:
                self.assertEqual(curses.pairs[pair][1], curses.COLOR_BLUE)
                self.assertFalse(theme.attr(role, True) & curses.A_REVERSE)


if __name__ == "__main__":
    unittest.main()
