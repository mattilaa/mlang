"""MLang source debugger: curses frontend to the installed LLDB engine."""
import argparse
from collections import deque
import importlib
import os
import re
from pathlib import Path
import subprocess
import sys


def load_lldb():
    try:
        return importlib.import_module("lldb")
    except ImportError:
        pass
    # lldb -P reports the binding location for both LLVM and Apple LLDB.
    try:
        result = subprocess.run([os.environ.get("MLADBG_LLDB", "lldb"), "-P"],
                                capture_output=True, text=True, timeout=10)
        if result.returncode == 0:
            sys.path.insert(0, result.stdout.strip())
        return importlib.import_module("lldb")
    except (ImportError, OSError, subprocess.SubprocessError) as error:
        raise RuntimeError("LLDB Python bindings are required. Install LLDB and "
                           "set MLADBG_PYTHON to its matching Python interpreter "
                           "(optionally MLADBG_LLDB to the LLDB executable). "
                           + str(error)) from error


HELP = """run [args] / r     launch (arguments can also follow -- on the CLI)
continue / c       resume         next / n       step over
step / s           step into      finish / f     step out
interrupt          stop a running process (Ctrl-C)
break / b NAME     function breakpoint; b FILE:LINE or b LINE
delete ID          remove breakpoint; enable/disable ID
bt                 backtrace      frame N        select frame
locals             locals and arguments
print / p EXPR     inspect variable or evaluate a C-compatible expression
threads            list threads   thread N       select thread index
watch VAR          write watchpoint on a variable
registers          registers      disassemble    assembly
memory ADDRESS     read memory    attach PID     attach to process
detach             detach         kill           terminate process
Any other command is passed to LLDB (including conditional breakpoints,
memory write, watchpoint options, source maps and expression assignment).
Expressions use LLDB's C/C++ syntax, not the MLang parser.
F5 continue, F6 next, F7 step, F8 finish, F9 break at current line.
Tab changes pane; arrows/PgUp/PgDn scroll; : enters a command; q quits.
"""


class Session:
    def __init__(self, lldb, executable, arguments, batch=False):
        self.lldb = lldb
        self.debugger = lldb.SBDebugger.Create(False)  # Do not source .lldbinit.
        self.debugger.SetUseColor(False)
        self.debugger.SetAsync(not batch)
        self.interpreter = self.debugger.GetCommandInterpreter()
        self.log = deque(maxlen=2000)
        self.batch = batch
        self.attached = False
        self.last_stop = None
        self.arguments = arguments
        self.target = self.debugger.CreateTarget(executable or "")
        if executable and not self.target.IsValid():
            raise RuntimeError("cannot load executable: " + executable)
        # Expose inferior stdin only through LLDB's explicit process input
        # command: it must not compete with curses for the terminal.
        info = self.target.GetLaunchInfo()
        info.SetArguments(arguments, False)
        info.AddOpenFileAction(0, os.devnull, True, False)
        info.SetLaunchFlags(info.GetLaunchFlags() & ~lldb.eLaunchFlagDisableASLR)
        self.target.SetLaunchInfo(info)

    def output(self, text):
        if text:
            self.log.extend(text.rstrip().splitlines())
            if self.batch:
                print(text, end="" if text.endswith("\n") else "\n", flush=True)

    def process(self):
        return self.target.GetProcess()

    def stopped(self):
        return self.process().GetState() in (self.lldb.eStateStopped,
                                             self.lldb.eStateCrashed,
                                             self.lldb.eStateSuspended)

    def frame(self):
        return self.process().GetSelectedThread().GetSelectedFrame()

    def poll(self):
        # Removing state-change events advances LLDB's public process state.
        # Without draining this listener an asynchronous launch can appear
        # stopped while frame access still reports "process is not stopped".
        event = self.lldb.SBEvent()
        listener = self.debugger.GetListener()
        while listener.GetNextEvent(event):
            pass
        self.target = self.debugger.GetSelectedTarget()
        process = self.process()
        if not process.IsValid():
            return
        for getter in (process.GetSTDOUT, process.GetSTDERR):
            while True:
                data = getter(4096)
                if not data:
                    break
                self.output(data)
        marker = (process.GetProcessID(), process.GetState(), process.GetStopID())
        if marker == self.last_stop:
            return
        self.last_stop = marker
        if self.stopped():
            # Select the thread that actually stopped, rather than a stale
            # selection left over from an earlier breakpoint.
            for thread in process:
                if thread.GetStopReason() not in (self.lldb.eStopReasonInvalid,
                                                  self.lldb.eStopReasonNone):
                    process.SetSelectedThread(thread)
                    break
            thread = process.GetSelectedThread()
            self.output("Stopped: " + (thread.GetStopDescription(512) or "unknown reason"))
            self.output(str(thread.GetSelectedFrame()))
        elif process.GetState() == self.lldb.eStateExited:
            self.output("Process exited with status " + str(process.GetExitStatus()))

    def command(self, text):
        text = text.strip()
        if not text:
            return True
        name, _, rest = text.partition(" ")
        if name in ("help", "h", "?"):
            self.output(HELP)
            return True
        if name in ("quit", "q", "exit"):
            return None
        aliases = {"r": "run", "c": "continue", "n": "next", "s": "step",
                   "f": "finish", "p": "print", "b": "break"}
        name = aliases.get(name, name)
        if name == "break":
            if not rest:
                self.output("Usage: break FUNCTION | FILE:LINE | LINE")
                return False
            # Strip outer quotes without splitting paths containing spaces.
            location = rest.strip().strip('"\'')
            file, sep, line = location.rpartition(":")
            if location.isdecimal():
                entry = self.frame().GetLineEntry()
                if not entry.IsValid():
                    self.output("A stopped source frame is required for b LINE")
                    return False
                bp = self.target.BreakpointCreateByLocation(entry.GetFileSpec(), int(location))
            elif sep and line.isdecimal():
                bp = self.target.BreakpointCreateByLocation(file, int(line))
            else:
                # MLang appends parameter types to overloaded symbols.
                # Restrict lookup to the executable to avoid library matches.
                bp = self.target.BreakpointCreateByRegex(
                    "^" + re.escape(location) + "(__.*)?$",
                    self.target.GetExecutable().GetFilename())
            self.output("Breakpoint %d: %d location(s)" % (bp.GetID(), bp.GetNumLocations()))
            return True
        if name == "print" and self.stopped():
            value = self.frame().FindVariable(rest)
            if value.IsValid():
                self.output("\n".join(format_value(value)))
                return True
        if name == "locals" and self.stopped():
            self.output("\n".join(value_lines(self.frame())))
            return True
        mappings = {"run": "process launch", "continue": "process continue",
                    "next": "thread step-over", "step": "thread step-in",
                    "finish": "thread step-out", "interrupt": "process interrupt",
                    "delete": "breakpoint delete", "enable": "breakpoint enable",
                    "disable": "breakpoint disable", "bt": "thread backtrace",
                    "frame": "frame select", "locals": "frame variable",
                    "print": "expression --", "threads": "thread list",
                    "thread": "thread select", "watch": "watchpoint set variable",
                    "registers": "register read", "memory": "memory read",
                    "attach": "process attach --pid", "detach": "process detach",
                    "kill": "process kill"}
        command = mappings.get(name, name)
        if name == "run" and rest:
            command += " --"
        command += (" " + rest) if rest else ""
        result = self.lldb.SBCommandReturnObject()
        self.interpreter.HandleCommand(command, result)
        self.output(result.GetOutput())
        self.output(result.GetError())
        if command.startswith("process attach") and result.Succeeded():
            self.attached = True
        elif command.startswith("process launch") and result.Succeeded():
            self.attached = False
        self.poll()
        return result.Succeeded()

    def close(self):
        process = self.process()
        if process.IsValid() and process.GetState() not in (self.lldb.eStateExited,
                                                           self.lldb.eStateDetached):
            if self.attached:
                process.Detach()
            else:
                process.Kill()
        self.lldb.SBDebugger.Destroy(self.debugger)


def format_value(value, depth=0, budget=None):
    """Inspect DWARF children without running code inside the debuggee.

    Bound both recursion and memory reads; a corrupt length or cyclic pointer
    must not freeze the terminal. Pointers are expanded only on explicit p *x.
    """
    budget = [128] if budget is None else budget
    if budget[0] <= 0:
        return []
    budget[0] -= 1
    name = value.GetName() or "value"
    type_ = value.GetType()
    type_name = type_.GetName() or "unknown"
    prefix = "  " * depth
    header = prefix + "(%s) %s" % (type_name, name)
    if value.GetError().Fail():
        return [header + " = <unavailable: %s>" % value.GetError().GetCString()]
    summary = value.GetSummary()
    scalar = value.GetValue()
    # LLDB presents DWARF 8-bit integers as C character types. MLang i8/u8
    # are numeric, so show their numeric value rather than a character escape.
    if type_name in ("unsigned char", "u8"):
        summary, scalar = None, str(value.GetValueAsUnsigned())
    elif type_name in ("signed char", "i8"):
        summary, scalar = None, str(value.GetValueAsSigned())
    if type_.IsPointerType() or value.GetNumChildren() == 0:
        return [header + " = " + (summary or scalar or "<unavailable>")]
    if depth >= 3:
        return [header + " = {...}"]
    length = value.GetChildMemberWithName("len")
    data = value.GetChildMemberWithName("data")
    keys = value.GetChildMemberWithName("keys")
    values = value.GetChildMemberWithName("values")
    collection = type_name.startswith(("list<", "array<", "multiarray<", "mutmultiarray<", "map<"))
    if collection and length.IsValid() and (data.IsValid() or keys.IsValid()):
        if length.GetError().Fail():
            return [header + " = <unavailable length>"]
        count = length.GetValueAsSigned()
        if count < 0:
            return [header + " = <invalid length %d>" % count]
        lines = [header + " = len=%d" % count]

        def element(pointer, i, label):
            element_type = pointer.GetType().GetPointeeType()
            size = element_type.GetByteSize()
            address = pointer.GetValueAsUnsigned()
            if pointer.GetError().Fail() or not address or not size:
                return None
            return pointer.CreateValueFromAddress(label, address + i * size, element_type)

        for i in range(min(count, 16)):
            if budget[0] <= 0:
                break
            if data.IsValid():
                item = element(data, i, "[%d]" % i)
                lines.extend(format_value(item, depth+1, budget) if item and item.IsValid()
                             else [prefix + "  [%d] = <unavailable data>" % i])
            else:
                key = element(keys, i, "[%d].key" % i)
                item = element(values, i, "[%d].value" % i)
                for entry, label in ((key, "key"), (item, "value")):
                    lines.extend(format_value(entry, depth+1, budget) if entry and entry.IsValid()
                                 else [prefix + "  [%d].%s = <unavailable data>" % (i, label)])
        if count > 16:
            lines.append(prefix + "  ... %d more element(s)" % (count-16))
        return lines
    lines = [header + " = {"]
    children = value.GetNumChildren()
    for i in range(min(children, 16)):
        if budget[0] <= 0:
            lines.append(prefix + "  ...")
            break
        lines.extend(format_value(value.GetChildAtIndex(i), depth+1, budget))
    if children > 16:
        lines.append(prefix + "  ... %d more field(s)" % (children-16))
    lines.append(prefix + "}")
    return lines


def value_lines(frame):
    if not frame.IsValid():
        return ["No selected frame"]
    values = frame.GetVariables(True, True, False, True)
    lines = []
    for value in values:
        lines.extend(format_value(value))
    return lines or ["No locals available (compile with -g -O0)"]


def safe_text(text):
    # Never let inferior output inject terminal escapes or control sequences.
    return "".join(c if c.isprintable() else " " for c in str(text))


def source_lines(session):
    frame = session.frame()
    entry = frame.GetLineEntry()
    if not entry.IsValid():
        return "Source", ["No source location. Compile with: mlang -g -O0 file.mla -o app"], 0
    spec = entry.GetFileSpec()
    path = Path(spec.GetDirectory() or "") / spec.GetFilename()
    # Respect LLDB source-map settings, including moved build trees.
    stream = session.lldb.SBStream()
    if session.target.GetSourceManager().DisplaySourceLinesWithLineNumbers(
            spec, entry.GetLine(), 12, 12, "=>", stream):
        lines = stream.GetData().splitlines()
        return str(path), lines, next((i for i, line in enumerate(lines) if "=>" in line), 0)
    return str(path), ["Source unavailable: " + str(path)], 0


def tui(screen, session):
    import curses
    curses.raw()  # Ctrl-C goes to the debuggee interrupt command, not SIGINT.
    curses.curs_set(0)
    screen.timeout(100)
    screen.keypad(True)
    command = ""
    editing = False
    focus = 0
    offsets = [0] * 5
    history = []
    history_index = 0
    source_position = None
    session.output("mladbg — type help for commands; F5 run/continue; : command; q quit")

    def put(y, x, text, width, attr=0):
        height, cols = screen.getmaxyx()
        if 0 <= y < height and 0 <= x < cols:
            try:
                screen.addnstr(y, x, safe_text(text), max(0, min(width, cols-x-1)), attr)
            except curses.error:
                pass  # Resize or bottom-right cell during a repaint.

    def pane(y, x, height, width, title, lines, index):
        put(y, x, ("> " if focus == index else "  ") + title, width, curses.A_REVERSE)
        offset = min(offsets[index], max(0, len(lines) - height + 1))
        for row, line in enumerate(lines[offset:offset + height - 1], 1):
            put(y + row, x, line, width)

    while True:
        session.poll()
        screen.erase()
        height, width = screen.getmaxyx()
        state = session.lldb.SBDebugger.StateAsCString(session.process().GetState())
        put(0, 0, " mladbg | %s | F5 continue F6 next F7 step F8 finish F9 break | : command q quit" % state,
            width, curses.A_REVERSE)
        if height < 18 or width < 70:
            put(2, 0, "Resize terminal to at least 70 columns x 18 rows", width)
        else:
            top = max(7, (height - 5) // 2)
            bottom = height - top - 4
            left = width * 2 // 3
            title, source, current = source_lines(session)
            position = (title, session.frame().GetPC())
            if position != source_position:
                offsets[0] = max(0, current - (top-2)//2)
                source_position = position
            pane(1, 0, top, left, title, source, 0)
            locals_ = value_lines(session.frame()) if session.stopped() else ["Process is " + state]
            pane(1, left, top, width-left, "Locals / arguments", locals_, 1)
            thread = session.process().GetSelectedThread()
            stack = [str(frame) for frame in thread] if session.stopped() else []
            console_height = max(3, bottom // 2)
            pane(top+1, 0, bottom-console_height, left, "Stack", stack, 2)
            breaks = [str(bp) for bp in session.target.breakpoint_iter()]
            pane(top+1, left, bottom-console_height, width-left, "Breakpoints", breaks, 3)
            # A dedicated console occupies the lower quarter on larger screens.
            pane(height-console_height-2, 0, console_height, width,
                 "Console / program output", list(session.log), 4)
            if focus != 4:
                offsets[4] = max(0, len(session.log) - console_height + 1)
        put(height-2, 0, "(mladbg) " + command if editing else "Tab: pane  arrows: scroll  : command  Ctrl-C: interrupt",
            width, curses.A_BOLD)
        screen.refresh()
        try:
            key = screen.get_wch()
        except curses.error:
            continue
        if key == "\x03":
            session.command("interrupt")
        elif editing:
            if key in ("\n", "\r", curses.KEY_ENTER):
                if command:
                    history.append(command)
                    session.output("(mladbg) " + command)
                    if session.command(command) is None:
                        break
                command = ""
                editing = False
            elif key == "\x1b":
                editing = False
                command = ""
            elif key in (curses.KEY_BACKSPACE, "\x7f", "\b"):
                command = command[:-1]
            elif key in (curses.KEY_UP, curses.KEY_DOWN):
                history_index = max(0, min(len(history), history_index +
                                          (-1 if key == curses.KEY_UP else 1)))
                command = history[history_index] if history_index < len(history) else ""
            elif isinstance(key, str) and key.isprintable():
                command += key
        elif key == "q":
            break
        elif key == ":":
            editing = True
            history_index = len(history)
        elif key == "\t":
            focus = (focus + 1) % 5
        elif key in (curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE):
            delta = {curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                     curses.KEY_PPAGE: -10, curses.KEY_NPAGE: 10}[key]
            offsets[focus] = max(0, offsets[focus] + delta)
        elif key == curses.KEY_F5:
            session.command("continue" if session.process().IsValid() and
                            session.process().GetState() != session.lldb.eStateExited else "run")
        elif key in (curses.KEY_F6, curses.KEY_F7, curses.KEY_F8):
            session.command({curses.KEY_F6: "next", curses.KEY_F7: "step",
                             curses.KEY_F8: "finish"}[key])
        elif key == curses.KEY_F9:
            entry = session.frame().GetLineEntry()
            if entry.IsValid():
                session.target.BreakpointCreateByLocation(entry.GetFileSpec(), entry.GetLine())


def main():
    parser = argparse.ArgumentParser(prog="mladbg", description="MLang terminal debugger powered by LLDB",
                                     epilog="Compile with mlang -g -O0 source.mla -o app. "
                                            "Use mladbg ./app -- program arguments.")
    parser.add_argument("--batch", action="store_true", help="run commands without the TUI")
    parser.add_argument("-ex", "--command", action="append", default=[], help="startup command (repeatable)")
    parser.add_argument("--attach", type=int, metavar="PID", help="attach to an existing process")
    parser.add_argument("--version", action="version", version="mladbg 0.1 (LLDB backend)")
    parser.add_argument("executable", nargs="?")
    parser.add_argument("arguments", nargs=argparse.REMAINDER)
    options = parser.parse_args()
    if not options.executable and not options.attach:
        parser.error("an executable or --attach PID is required")
    if not options.batch and (not sys.stdin.isatty() or not sys.stdout.isatty()):
        parser.error("the TUI requires a terminal; use --batch -ex COMMAND for scripts")
    session = None
    try:
        lldb = load_lldb()
        lldb.SBDebugger.Initialize()
        arguments = options.arguments
        if arguments[:1] == ["--"]:
            arguments = arguments[1:]
        session = Session(lldb, options.executable, arguments, options.batch)
        commands = (["attach " + str(options.attach)] if options.attach else []) + options.command
        for command in commands:
            result = session.command(command)
            if result is None:
                return 0
            if not result:
                if not options.batch:
                    print("\n".join(session.log), file=sys.stderr)
                return 1
        if not options.batch:
            import curses
            curses.wrapper(tui, session)
        return 0
    except (RuntimeError, OSError, KeyboardInterrupt) as error:
        print("mladbg: " + str(error), file=sys.stderr)
        return 1
    finally:
        if session:
            session.close()


if __name__ == "__main__":
    sys.exit(main())
