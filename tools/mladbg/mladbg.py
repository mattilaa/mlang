"""MLang source debugger: curses frontend to the installed LLDB engine."""
import argparse
from collections import deque
import importlib
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
from ui import Theme, Glyphs, VariableTree, FileCompletion, token_spans


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


HELP_TOPICS = {
    "overview": """MLADBG HELP

Press : to enter a command. Press Enter to execute it.
F1 or ? opens help; Esc, F1, ? or q closes help.
In help, use 0-8 or Tab to choose a topic:

  0  Overview
  1  Frames       bt, frame N, up/down, per-frame variables
  2  Execution    run, continue, next, step, finish, interrupt
  3  Variables    locals, print, nested values, expressions
  4  Breakpoints  source/function breaks, conditions, watches
  5  Threads      choose a thread and inspect its stack
  6  Memory       registers, assembly, memory reads/writes
  7  Session      launch arguments, attach, detach, quit
  8  Keys         pane focus, Vim scrolling, command history

Commands: help frames, help variables, help keys, etc.
For native command details: help lldb breakpoint modify
Other debugger commands are passed directly to LLDB.

Quick start:
  b main       set a function breakpoint
  run          launch and stop there
  next         step past initialization
  locals       inspect arguments and local variables
  bt           list the call stack
  frame 1      inspect the caller
  p count      print count in that frame
  frame 0      return to the current function before stepping
  continue     resume execution
""",
    "frames": """STACK FRAMES

Frame 0 is the current function. Higher numbers are callers.
Frame commands require a stopped process (breakpoint or Ctrl-C).

  bt              list frames in the selected thread
  frame 0         select the current function
  frame 1         select its caller
  frame N         jump directly to any listed frame
  up              select the next caller (higher number)
  down            move toward the current function
  locals          print the selected frame's locals/arguments
  p variable      inspect a variable in the selected frame

The source and locals panes follow your selection. The stack
pane marks the selected frame with >. Scrolling the stack pane
does not select a frame; use frame N, up or down.

Selecting a frame keeps execution paused. Choose frame 0 before
F6/F7/F8 to step from the current function. finish runs until
that function returns; next then completes the caller's store.
A caller's result variable may be uninitialized until then.

Example at a nested breakpoint:
  bt
  frame 1
  locals
  p budget
  frame 2
  p team.members
  frame 0
  finish
  next
  p result
""",
    "execution": """EXECUTION AND STEPPING

  run / r [args]  launch the target (F5 when not running)
  continue / c    resume until a breakpoint or exit (F5)
  next / n        step over a source statement (F6)
  step / s        step into a function call (F7)
  finish / f      run until the selected function returns (F8)
  interrupt      pause a running process (Ctrl-C)
  kill           terminate the current process

Use frame 0 before stepping from the innermost function.
Step past declarations before inspecting their initialized values.
next executes called functions; step enters them. finish executes
the remaining body rather than simply changing the selected frame.

Compile with -g -O0 for predictable source stepping and locals:
  mlang -g -O0 examples/debugger_demo.mla -o app
""",
    "variables": """VARIABLES AND COMPLEX VALUES

Variables belong to the selected frame and thread.
  locals                   show locals and arguments
  p count                  inspect a scalar
  p team                   expand a nested struct
  p team.members           preview a list of structs
  p team.ratings           preview a map's keys and values
  p numbers.data[1]        inspect a collection element
  p pair._0                inspect a tuple field
  p pointer->position.x    follow a pointer field explicitly
  p *pointer               inspect the pointee
  p count + 1              evaluate an expression
  p count = 10             change a value (when writable)

Structs/tuples expand; lists/arrays/maps preview their elements.
Previews stop at 16 elements, 3 nested levels and 128 values.
These limits apply to p/locals output; the TUI uses a lazy tree:
  Tab to Locals; j/Down moves down, k/Up moves up
  l/Right opens a node; h/Left closes it or its parent
  Shift-J collapses all; Shift-K restores previous expansions
Structures start collapsed. Expansion is remembered per frame.
The tree shows 16 children per node, up to 8 levels/256 rows.
Opening a pointer node explicitly reads its pointee.
Pointers are not followed automatically. str8/str16 show text;
numeric enums show variant names. Unavailable values are marked.

Expressions use LLDB's C/C++ syntax, not MLang syntax. Explicit
expressions can change state or call functions in the program.
Optimized or uninitialized variables may not have useful values.
""",
    "breakpoints": """BREAKPOINTS AND WATCHPOINTS

  b main                   break on a function
  b review                 also resolves MLang overloads
  b file.mla:26            break at a source line
  b 26                     line in the selected source frame
  F9                       break at the current execution line
  breakpoint list          list IDs and locations
  disable 1 / enable 1     toggle a breakpoint
  delete 1                 remove it
  breakpoint modify -c 'count > 5' 1
                           set a C-compatible condition

Pending breakpoints have no resolved locations yet. A source
line without executable code may resolve to the next statement.
F9 uses the selected frame's execution line, not a scrolled row.

  watch count              watch writes to a variable
  watchpoint list          list watchpoints
  watchpoint delete 1      remove a watchpoint
  watchpoint set variable -w read count
                           watch reads (if supported)
Watchpoints require stopped variable storage and hardware support.
""",
    "threads": """THREADS

  threads         list threads and their index numbers
  thread 2        select thread index 2 (not its OS thread ID)
  bt              inspect that thread's call stack
  frame 0         select its current function
  locals          inspect its arguments and locals
  p variable      print a value in its selected frame

When a process stops, mladbg selects a thread with a stop reason.
Select another thread while paused to inspect its own stack.
continue resumes execution; it does not just change thread focus.
""",
    "memory": """REGISTERS, ASSEMBLY AND MEMORY

  registers                   read the selected frame's registers
  disassemble                 show the selected function's assembly
  asm mixed                   source/assembly split view (a cycles views)
  asm line / asm function     selected source line / function instructions
  si / ni                     step into / over one instruction
  patch pc nop                replace the current instruction with NOPs
  patch 0xADDRESS ASM          equal-size assembly replacement
  patch-bytes 0xADDRESS HEX    equal-size raw bytes, e.g. 90 90
  patch list / patch undo     inspect edits / undo the last edit

In assembly: j/k select, e edits the selected instruction, b sets an
address breakpoint, i/I instruction-step/step-over, u undoes a patch.
Bytes and source locations are shown; => marks the selected frame PC.
Assembly edits use clang (MLADBG_CLANG overrides its path). x86 uses
AT&T syntax. Labels/directives/relocations are rejected. NOP fills the
whole selected instruction. Patches affect stopped process memory only;
they do not change source or the executable. Continue explicitly to run.
Wrong instructions can corrupt/crash the process. Code-signing/W^X or
remote-target restrictions may refuse writes; an error is displayed.
  memory 0xADDRESS            read memory at an address
  memory read -f x -s 1 -c 16 0xADDRESS
                              read 16 bytes in hexadecimal
  memory write -s 1 0xADDRESS 0xff
                              write a byte
  thread step-inst            step into one machine instruction
  thread step-inst-over       step over one machine instruction

Addresses and register names depend on the target architecture.
Use p &variable to locate its storage in the selected frame.
Native options: help lldb memory read / help lldb disassemble
""",
    "session": """LAUNCHING, ATTACHING AND SOURCE FILES

  mladbg ./app -- argument1 argument2
                              start with program arguments
  run argument1 argument2      launch with new arguments
  mladbg --attach PID          attach from the command line
  attach PID                   attach inside the debugger
  detach                       release an attached process
  kill                         terminate a process
  quit / q / exit              close the debugger

Quitting kills a process launched here and detaches one attached
here. Inferior stdin defaults to /dev/null; for input redirection:
  process launch -i input.txt -- argument1

For moved source trees:
  settings set target.source-map OLD_DIRECTORY NEW_DIRECTORY

Batch mode (options before the executable):
  mladbg --batch -ex 'b main' -ex run -ex locals ./app
Initialization files are not loaded automatically.
Colors are automatic; use mladbg --no-colors ./app for monochrome.
Box glyphs are automatic on UTF-8 terminals; --no-glyphs uses ASCII.
""",
    "keys": """KEYBOARD AND COMMAND ENTRY

  F1 / ?           open help; close it with Esc/F1/?/q
  F5               run or continue
  F6 / F7 / F8     next / step / finish
  F9               breakpoint at the selected execution line
  Ctrl-C           interrupt a running process
  Tab              focus the next pane
  Shift-Tab        focus the previous pane (wraps around)
  a                cycle source / mixed source+assembly / assembly views
  j / k            scroll the focused pane down / up
  h / l            scroll it left / right
  arrow keys       same scroll directions
  PgUp / PgDn      scroll by ten rows
  :                start entering a command
  q                quit (outside command entry or help)

Inside command entry (command history uses Up/Down):
  Enter            execute the command
  Esc              cancel it
  Up / Down        recall previous / next command
  Tab              open file completion (b/break, file, target create,
                   command source); :b then Tab lists the current directory
  In completion    j/k or Up/Down select; Enter/Tab accepts a file or
                   browses a directory; Left browses the parent; Esc closes
                   without cancelling the command. Enter again executes.
  Backspace        delete the last character
  h/j/k/l          type normal letters

In the Locals pane (structures start collapsed):
  j/Down, k/Up     select the next/previous variable or field
  l / Right       expand; on an open node, enter its first child
  h / Left        collapse; on a closed child, return to its parent
  Shift-J         collapse all nodes
  Shift-K         restore the expansions saved by Shift-J
  PgUp/PgDn       move the selection by ten rows
j/k and Up/Down never change which nodes are expanded.

Inside help: 0-8 choose a topic; Tab cycles topics. Scrolling keys
still work. The program stays in its current execution state.
--no-colors disables the palette; unsupported terminals fall back
to monochrome. Source keywords/types/strings/numbers are colored;
the current source line and focused tree selection use blue.
Use --no-glyphs for ASCII borders and escaped non-ASCII display text.
""",
}
HELP_ORDER = tuple(HELP_TOPICS)
HELP_ALIASES = {"stack": "frames", "frame": "frames", "stepping": "execution",
                "locals": "variables", "print": "variables", "break": "breakpoints",
                "asm": "memory", "assembly": "memory", "patch": "memory",
                "keyboard": "keys"}
HELP = HELP_TOPICS["overview"]


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
        self.asm_mode = "source"
        self.asm_scope = "function"
        self.patch_history = []
        self.code_revision = 0
        self.asm_cache = (None, [])
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
        if name == "asm":
            option = rest.strip() or "mixed"
            if option in ("source", "mixed", "assembly", "off"):
                self.asm_mode = "source" if option == "off" else option
            elif option in ("line", "function"):
                self.asm_scope = option
                self.asm_mode = "mixed"
            else:
                self.output("Usage: asm [source|mixed|assembly|line|function|off]")
                return False
            self.output("Assembly view: %s (%s)" % (self.asm_mode, self.asm_scope))
            if self.batch and self.stopped():
                self.output("\n".join(row[1] for row in self.assembly()))
            return True
        if name in ("patch", "patch-bytes"):
            try:
                return self.patch(rest, raw=name == "patch-bytes")
            except (ValueError, OSError, subprocess.SubprocessError) as error:
                self.output("Patch refused: " + str(error))
                return False
        if name in ("help", "h", "?"):
            topic = rest.strip().lower() or "overview"
            topic = HELP_ALIASES.get(topic, topic)
            if topic in HELP_TOPICS:
                self.output(HELP_TOPICS[topic])
                return True
            if topic == "lldb" or topic.startswith("lldb "):
                name, rest = "help", rest.strip()[4:].strip()
            else:
                self.output("Unknown help topic. Use help or help lldb COMMAND.")
                return False
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
            if not value.IsValid() and re.fullmatch(
                    r"[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*|\[\d+\])*", rest):
                value = self.frame().GetValueForVariablePath(rest, self.lldb.eDynamicDontRunTarget)
            if value.IsValid():
                self.output("\n".join(format_value(value)))
                return True
        if name == "locals" and self.stopped():
            self.output("\n".join(value_lines(self.frame())))
            return True
        mappings = {"run": "process launch", "continue": "process continue",
                    "si": "thread step-inst", "ni": "thread step-inst-over",
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

    def assembly(self):
        """Bounded, cached instructions from the selected frame's function."""
        if not self.stopped() or not self.frame().IsValid():
            return []
        frame, target = self.frame(), self.target
        key = (self.process().GetProcessID(), self.process().GetStopID(),
               frame.GetPC(), self.code_revision, self.asm_scope)
        if key == self.asm_cache[0]:
            return self.asm_cache[1]
        start = frame.GetFunction().GetStartAddress()
        if not start.IsValid():
            start = frame.GetSymbol().GetStartAddress()
        end = frame.GetFunction().GetEndAddress()
        if not end.IsValid():
            end = frame.GetSymbol().GetEndAddress()
        if not start.IsValid():
            start = frame.GetPCAddress()
        flavor = "att" if target.GetTriple().split("-", 1)[0] in ("x86_64", "i386", "i686") else "default"
        instructions = target.ReadInstructions(start, 256, flavor)
        rows = []
        selected_entry = frame.GetLineEntry()
        for instruction in instructions:
            address = instruction.GetAddress()
            load = address.GetLoadAddress(target)
            if end.IsValid() and load >= end.GetLoadAddress(target):
                break
            entry = address.GetLineEntry()
            same_line = (entry.IsValid() and selected_entry.IsValid() and
                         entry.GetFileSpec() == selected_entry.GetFileSpec() and
                         entry.GetLine() == selected_entry.GetLine())
            if self.asm_scope == "line" and not same_line and load != frame.GetPC():
                continue
            error = self.lldb.SBError()
            data = self.process().ReadMemory(load, instruction.GetByteSize(), error)
            encoded = data.hex(" ") if error.Success() else "??"
            location = ("%s:%d" % (entry.GetFileSpec().GetFilename(), entry.GetLine())
                        if entry.IsValid() else "no source")
            text = "%s 0x%x  %-22s %s %s  // %s" % (
                "=>" if load == frame.GetPC() else "  ", load, encoded,
                instruction.GetMnemonic(target) or "?", instruction.GetOperands(target) or "",
                location)
            rows.append((load, text))
        self.asm_cache = (key, rows)
        return rows

    def patch(self, text, raw=False):
        if not self.stopped():
            raise ValueError("stop the process before editing code")
        process = self.process()
        pid = process.GetProcessID()
        self.patch_history = [p for p in self.patch_history if p[0] == pid]
        if text.strip() == "list":
            for _, address, original, replacement in self.patch_history:
                self.output("0x%x: %s -> %s" % (address, original.hex(" "), replacement.hex(" ")))
            if not self.patch_history:
                self.output("No live patches.")
            return True
        undo = text.strip() == "undo"
        if undo:
            if not self.patch_history:
                raise ValueError("no patch to undo in this process")
            _, address, replacement, original = self.patch_history[-1]
        else:
            location, _, assembly = text.strip().partition(" ")
            if not assembly:
                raise ValueError("usage: patch PC|0xADDRESS ASM | patch undo | patch list")
            pc_relative = re.fullmatch(r"pc(?:([+-])(0x[0-9a-fA-F]+|[0-9]+))?", location.lower())
            if pc_relative:
                address = self.frame().GetPC()
                if pc_relative[1]:
                    address += int(pc_relative[2], 0) * (1 if pc_relative[1] == "+" else -1)
            else:
                address = int(location, 0)
            # Only known instruction starts are accepted, never the middle of one.
            scope = self.asm_scope
            self.asm_scope = "function"
            try:
                starts = {row[0] for row in self.assembly()}
            finally:
                self.asm_scope = scope
            if address not in starts:
                raise ValueError("choose an instruction address in the selected function (first 256 instructions)")
            instruction = self.target.ReadInstructions(self.target.ResolveLoadAddress(address), 1).GetInstructionAtIndex(0)
            size = instruction.GetByteSize()
            replacement = bytes.fromhex(assembly) if raw else assemble_instruction(assembly, self.target.GetTriple(), size)
            if not replacement or len(replacement) != size:
                raise ValueError("replacement must occupy exactly %d byte(s); got %d" % (size, len(replacement)))
            error = self.lldb.SBError()
            original = process.ReadMemory(address, size, error)
            if error.Fail() or len(original) != size:
                raise ValueError("cannot read original instruction: " + str(error))
        error = self.lldb.SBError()
        # Check stale undo records before overwriting another tool's edits.
        current = process.ReadMemory(address, len(replacement), error)
        if error.Fail() or current != original:
            raise ValueError("code changed since this patch; refusing to overwrite it")
        count = process.WriteMemory(address, replacement, error)
        if error.Fail() or count != len(replacement):
            rollback = self.lldb.SBError()
            process.WriteMemory(address, original, rollback)
            raise ValueError("code write failed: %s; rollback: %s" % (error, rollback))
        self.code_revision += 1
        verify = process.ReadMemory(address, len(replacement), error)
        if error.Fail() or verify != replacement:
            rollback = self.lldb.SBError()
            process.WriteMemory(address, original, rollback)
            raise ValueError("patch verification failed; rollback: %s" % rollback)
        if undo:
            self.patch_history.pop()
        else:
            self.patch_history.append((pid, address, original, replacement))
        self.output("%s 0x%x: %s -> %s (process memory only)" % (
            "Restored" if undo else "Patched", address, original.hex(" "), replacement.hex(" ")))
        return True

    def close(self):
        process = self.process()
        if process.IsValid() and process.GetState() not in (self.lldb.eStateExited,
                                                           self.lldb.eStateDetached):
            if self.attached:
                process.Detach()
            else:
                process.Kill()
        self.lldb.SBDebugger.Destroy(self.debugger)


def assemble_instruction(text, triple, size):
    """Use LLVM's assembler; reject symbolic fixups and assembly directives."""
    arch = triple.split("-", 1)[0]
    if text.strip().lower() == "nop":
        if arch in ("x86_64", "i386", "i686"):
            return b"\x90" * size
        if arch in ("arm64", "aarch64") and size == 4:
            return bytes.fromhex("1f 20 03 d5")
    statements = text.split(";")
    if "\n" in text or "\r" in text or any(
            not re.fullmatch(r"\s*[A-Za-z][A-Za-z0-9]*\s*(?:[^\n\r:]*)", s) for s in statements):
        raise ValueError("only instructions are allowed; no directives, labels or newlines")
    assembler = shutil.which(os.environ.get("MLADBG_CLANG", "clang"))
    if not assembler:
        raise ValueError("clang is required for assembly edits (or use patch-bytes)")
    result = subprocess.run([assembler, "-cc1as", "-triple", triple, "-filetype", "asm",
                             "-show-encoding", "-o", "-", "-"],
                            input="\n".join(statements)+"\n", text=True,
                            capture_output=True, timeout=10)
    if result.returncode:
        raise ValueError(result.stderr.strip())
    encodings = re.findall(r"encoding:\s*\[([^\]]*)\]", result.stdout)
    if len(encodings) != len(statements):
        raise ValueError("assembler did not emit one encoding per instruction")
    data = bytearray()
    for encoding in encodings:
        for byte in encoding.split(","):
            if not re.fullmatch(r"\s*0x[0-9a-fA-F]{2}\s*", byte):
                raise ValueError("symbolic/PC-relative relocations are unsupported; use resolved bytes")
            data.append(int(byte, 16))
    return bytes(data)


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
        if not session.process().IsValid():
            return "Source", ["Program loaded. Press F5 to run.",
                              "Use :b main to set a breakpoint before running."], 0
        if not session.stopped():
            return "Source", ["Source appears when execution stops.",
                              "Ctrl-C interrupts a running program."], 0
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


def tui(screen, session, use_colors=True, use_glyphs=True):
    import curses
    curses.raw()  # Ctrl-C goes to the debuggee interrupt command, not SIGINT.
    curses.curs_set(0)
    screen.timeout(100)
    screen.keypad(True)
    command = ""
    editing = False
    completion = None
    focus = 0
    offsets = [0] * 7
    horizontal_offsets = [0] * 7
    help_topic = None
    history = []
    history_index = 0
    source_position = None
    assembly_position = None
    assembly_cursor = 0
    assembly_rows = []
    locals_tree = VariableTree()
    session.output("mladbg — type help for commands; F5 run/continue; : command; q quit")
    theme = Theme(curses, use_colors)
    glyphs = Glyphs(use_glyphs, getattr(screen, "encoding", None) or sys.stdout.encoding or "ascii")
    if not theme.enabled:
        # curses.wrapper may initialize color support itself. Keep pair 0
        # at the terminal defaults even when application colors are disabled.
        try:
            curses.use_default_colors()
        except curses.error:
            pass
    border_attr = theme.attr("border") | curses.A_DIM
    focus_attr = theme.attr("border") | curses.A_BOLD

    def put(y, x, text, width, attr=0):
        height, cols = screen.getmaxyx()
        if 0 <= y < height and 0 <= x < cols:
            try:
                screen.addnstr(y, x, glyphs.text(safe_text(text)), max(0, min(width, cols-x-1)), attr)
            except curses.error:
                pass  # Resize or bottom-right cell during a repaint.

    def pane(y, x, height, width, title, lines, index):
        attr = focus_attr if focus == index or index == 5 else border_attr

        def cell(row, col, character):
            try:
                screen.addch(row, col, character, attr)
            except curses.error:
                pass

        for col in range(x+1, x+width-1):
            cell(y, col, glyphs.horizontal)
            cell(y+height-1, col, glyphs.horizontal)
        for row in range(y+1, y+height-1):
            cell(row, x, glyphs.vertical)
            cell(row, x+width-1, glyphs.vertical)
        for row, col, character in ((y, x, glyphs.top_left),
                                    (y, x+width-1, glyphs.top_right),
                                    (y+height-1, x, glyphs.bottom_left),
                                    (y+height-1, x+width-1, glyphs.bottom_right)):
            cell(row, col, character)
        put(y, x+2, " " + title + " ", width-4, attr)
        content_height = height-2
        offsets[index] = min(offsets[index], max(0, len(lines)-content_height))
        content_width = width-4
        longest = max((len(glyphs.text(safe_text(line))) for line in lines), default=0)
        horizontal_offsets[index] = min(horizontal_offsets[index], max(0, longest-content_width))
        offset = offsets[index]
        for row, line in enumerate(lines[offset:offset+content_height], 1):
            selected = (index == 0 and line.lstrip().startswith("=>") or
                        index == 6 and (line.lstrip().startswith("=>") or
                                        focus == 6 and offset+row-1 == assembly_cursor) or
                        index == 1 and focus == 1 and offset+row-1 == locals_tree.cursor and session.stopped())
            line = glyphs.text(safe_text(line))
            start = horizontal_offsets[index]
            visible = line[start:start+content_width]
            role = "error" if index == 4 and line.lower().startswith("error:") else "text"
            put(y+row, x+2, visible.ljust(content_width), content_width, theme.attr(role, selected))
            if index in (0, 1, 6):
                for begin, end, token_role in token_spans(line):
                    begin, end = max(begin, start), min(end, start+content_width)
                    if begin < end:
                        put(y+row, x+2+begin-start, line[begin:end], end-begin,
                            theme.attr(token_role, selected))

    while True:
        session.poll()
        screen.erase()
        height, width = screen.getmaxyx()
        state = session.lldb.SBDebugger.StateAsCString(session.process().GetState())
        ready = not session.process().IsValid()
        status = "Ready" if ready else state.capitalize()
        put(0, 0, " mladbg | %s | F1 help | F5 run/continue  F6 next  F7 step  F8 finish  F9 break" % status,
            width, curses.A_BOLD)
        if help_topic is not None:
            if height >= 8 and width >= 40:
                pane(1, 0, height-3, width, "Help | " + help_topic.title(),
                     HELP_TOPICS[help_topic].splitlines(), 5)
            else:
                put(2, 0, "Resize to 40 columns x 8 rows for help", width)
        elif height < 18 or width < 70:
            put(2, 0, "Resize terminal to at least 70 columns x 18 rows", width)
        else:
            gap = 1 if height >= 22 else 0
            available = height-3-2*gap
            top = max(5, available//2)
            console_height = max(4, available//4)
            middle = available-top-console_height
            left = (width-1)*3//5
            right_x = left+1
            middle_y = 1+top+gap
            console_y = middle_y+middle+gap
            title, source, current = source_lines(session)
            source_height = top//2 if session.asm_mode == "mixed" and top >= 10 else top
            position = (title, session.frame().GetPC(), source_height)
            if position != source_position:
                offsets[0] = max(0, current - (source_height-3)//2)
                source_position = position
            source_title = "Source" if title == "Source" else "Source | " + Path(title).name
            assembly_rows = session.assembly() if session.asm_mode != "source" else []
            assembly_text = [row[1] for row in assembly_rows] or ["Run and stop to inspect assembly."]
            asm_position = (session.process().GetProcessID(), session.process().GetStopID(),
                            session.frame().GetPC(), session.asm_scope, session.code_revision,
                            session.asm_mode)
            if asm_position != assembly_position:
                assembly_cursor = next((i for i, row in enumerate(assembly_rows)
                                        if row[0] == session.frame().GetPC()), 0)
                offsets[6] = max(0, assembly_cursor-2)
                assembly_position = asm_position
            mixed = session.asm_mode == "mixed" and top >= 10
            if session.asm_mode == "source" and focus == 6:
                focus = 0
            elif session.asm_mode != "source" and not mixed and focus == 0:
                focus = 6
            if session.asm_mode == "source":
                pane(1, 0, top, left, source_title, source, 0)
            else:
                asm_y, asm_height = (1+top//2, top-top//2) if mixed else (1, top)
                if mixed:
                    pane(1, 0, top//2, left, source_title, source, 0)
                offsets[6] = min(offsets[6], assembly_cursor)
                offsets[6] = max(offsets[6], assembly_cursor-asm_height+3)
                assembly_text = [("> " if focus == 6 and i == assembly_cursor else "  ")+line
                                 for i, line in enumerate(assembly_text)]
                pane(asm_y, 0, asm_height, left, "Assembly | " + session.asm_scope, assembly_text, 6)
            if session.stopped():
                frame = session.frame()
                thread = session.process().GetSelectedThread()
                context = (session.process().GetProcessID(), thread.GetThreadID(),
                           frame.GetCFA(), frame.GetFunctionName())
                locals_ = locals_tree.refresh(frame, context)
                if locals_tree.rows:
                    cursor = locals_tree.cursor
                    offsets[1] = min(offsets[1], cursor)
                    offsets[1] = max(offsets[1], cursor-(top-2)+1)
                    locals_ = [("> " if i == cursor else "  ")+text for i, text in enumerate(locals_)]
                else:
                    locals_ = ["No locals available (compile with -g -O0)"]
            else:
                locals_ = ["Run and stop to inspect variables." if ready else "Process is " + state]
            pane(1, right_x, top, width-right_x, "Locals / arguments", locals_, 1)
            thread = session.process().GetSelectedThread()
            stack = []
            if session.stopped():
                selected = thread.GetSelectedFrame().GetFrameID()
                for frame in thread:
                    function = frame.GetFunctionName() or "<unknown>"
                    function = function.split("__", 1)[0] or function
                    entry = frame.GetLineEntry()
                    location = ("  %s:%d" % (entry.GetFileSpec().GetFilename(), entry.GetLine())
                                if entry.IsValid() else "")
                    stack.append("%s %d  %s%s" % (">" if frame.GetFrameID() == selected else " ",
                                                   frame.GetFrameID(), function, location))
            stack = stack or ["No stopped stack frames."]
            pane(middle_y, 0, middle, left, "Stack", stack, 2)
            breaks = []
            for bp in session.target.breakpoint_iter():
                count = bp.GetNumLocations()
                entry = bp.GetLocationAtIndex(0).GetAddress().GetLineEntry() if count else None
                location = ("%s:%d" % (entry.GetFileSpec().GetFilename(), entry.GetLine())
                            if entry and entry.IsValid() else "%d location(s)" % count)
                breaks.append("%d %s  %s" % (bp.GetID(), "on" if bp.IsEnabled() else "off", location))
                if bp.GetCondition():
                    breaks.append("  if " + bp.GetCondition())
            pane(middle_y, right_x, middle, width-right_x, "Breakpoints",
                 breaks or ["Use :b FUNCTION or FILE:LINE"], 3)
            # A dedicated console occupies the lower quarter on larger screens.
            if focus != 4:
                offsets[4] = max(0, len(session.log)-console_height+2)
            pane(console_y, 0, console_height, width,
                 "Console / program output", list(session.log), 4)
        if help_topic is not None:
            put(height-2, 0, "0-8/Tab: topics | j/k/PgUp/PgDn: scroll", width, curses.A_BOLD)
            put(height-1, 0, "Esc/F1/?/q: close help | Ctrl-C: interrupt", width, curses.A_DIM)
        else:
            put(height-2, 0, "(mladbg) " + command if editing else ": command | F5 run/continue | Ctrl-C: stop | q quit",
                width, curses.A_BOLD)
            hints = ("j/k: down/up | h: close | l: open | J/K: all/restore"
                     if focus == 1 else "j/k: select | e: edit | b: break | i/I: step | u: undo | a: view"
                     if focus == 6 else "Tab/Shift-Tab: pane | hjkl/arrows: scroll | a: asm | F1/? help")
            put(height-1, 0, hints, width, curses.A_DIM)
        if completion is not None and editing and height >= 8 and width >= 30:
            visible_rows = min(8, height-6, max(1, len(completion.choices)))
            popup_height = visible_rows+3
            popup_width = min(width-2, max(36, min(78, max(
                (len(glyphs.text(path))+6 for path, _ in completion.choices), default=36))))
            x = min(8+len(completion.prefix), width-popup_width-1)
            y = height-2-popup_height
            for row in range(popup_height):
                put(y+row, x, " "*popup_width, popup_width)
            edge = glyphs.top_left+glyphs.horizontal*(popup_width-2)+glyphs.top_right
            put(y, x, edge, popup_width, focus_attr)
            put(y, x+2, " Files | "+(completion.fragment or "./")+" ", popup_width-4, focus_attr)
            start = max(0, completion.cursor-visible_rows+1)
            for row in range(visible_rows):
                put(y+row+1, x, glyphs.vertical+" "*(popup_width-2)+glyphs.vertical, popup_width, focus_attr)
                i = start+row
                if i < len(completion.choices):
                    path, directory = completion.choices[i]
                    selected = i == completion.cursor
                    put(y+row+1, x+2, (("> " if selected else "  ")+glyphs.text(path)).ljust(popup_width-4),
                        popup_width-4, theme.attr("type" if directory else "text", selected))
            put(y+popup_height-2, x, glyphs.vertical+" "*(popup_width-2)+glyphs.vertical, popup_width, focus_attr)
            put(y+popup_height-2, x+2, completion.message, popup_width-4)
            put(y+popup_height-1, x, glyphs.bottom_left+glyphs.horizontal*(popup_width-2)+glyphs.bottom_right,
                popup_width, focus_attr)
            put(height-1, 0, "j/k/arrows: select | Enter/Tab: choose | Left: parent | Esc: close".ljust(width),
                width, curses.A_DIM)
        screen.refresh()
        try:
            key = screen.get_wch()
        except curses.error:
            continue
        if key == "\x03":
            completion = None
            session.command("interrupt")
        elif completion is not None and editing:
            if key == "\x1b":
                completion = None
            elif key in ("j", "k", curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE):
                completion.move({"j": 1, "k": -1, curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                                 curses.KEY_PPAGE: -8, curses.KEY_NPAGE: 8}[key])
            elif key in ("\t", "\n", "\r", curses.KEY_ENTER, curses.KEY_RIGHT):
                had_choices = bool(completion.choices)
                completed = completion.accept()
                if completed is not None:
                    command = completed
                    completion = None
                elif had_choices:
                    command = completion.prefix+completion.fragment+completion.suffix
            elif key == curses.KEY_LEFT:
                completion.parent()
                command = completion.prefix+completion.fragment+completion.suffix
            elif key in (curses.KEY_BACKSPACE, "\x7f", "\b"):
                command = command[:-1]
                completion = FileCompletion(command)
            elif isinstance(key, str) and key.isprintable():
                command += key
                completion = FileCompletion(command)
        elif help_topic is not None:
            if key in ("\x1b", "q", "?", curses.KEY_F1):
                help_topic = None
            elif key == "\t" or isinstance(key, str) and key in "012345678":
                index = ((HELP_ORDER.index(help_topic)+1) % len(HELP_ORDER)
                         if key == "\t" else int(key))
                help_topic = HELP_ORDER[index]
                offsets[5] = horizontal_offsets[5] = 0
            elif key in ("j", "k", curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE):
                delta = {"j": 1, "k": -1, curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                         curses.KEY_PPAGE: -10, curses.KEY_NPAGE: 10}[key]
                offsets[5] = max(0, offsets[5]+delta)
            elif key in ("h", "l", curses.KEY_LEFT, curses.KEY_RIGHT):
                delta = -1 if key in ("h", curses.KEY_LEFT) else 1
                horizontal_offsets[5] = max(0, horizontal_offsets[5]+delta)
        elif key == curses.KEY_F1 or key == "?" and not editing:
            help_topic = "overview"
            offsets[5] = horizontal_offsets[5] = 0
        elif editing:
            if key == "\t":
                completion = FileCompletion(command)
                if completion.prefix:
                    command = completion.prefix+completion.fragment+completion.suffix
            elif key in ("\n", "\r", curses.KEY_ENTER):
                if command:
                    history.append(command)
                    session.output("(mladbg) " + command)
                    result = session.command(command)
                    if result is None:
                        break
                    parts = command.strip().split(maxsplit=1)
                    if result and parts and parts[0] in ("help", "h", "?"):
                        topic = parts[1].strip().lower() if len(parts) > 1 else "overview"
                        topic = HELP_ALIASES.get(topic, topic)
                        if topic in HELP_TOPICS:
                            help_topic = topic
                            offsets[5] = horizontal_offsets[5] = 0
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
        elif key in ("\t", curses.KEY_BTAB):
            panes = ([0] if session.asm_mode == "source" else
                     [0, 6] if session.asm_mode == "mixed" and height >= 26 else [6]) + [1, 2, 3, 4]
            direction = -1 if key == curses.KEY_BTAB else 1
            focus = panes[(panes.index(focus)+direction) % len(panes)] if focus in panes else panes[0]
        elif key == "a":
            modes = ("source", "mixed", "assembly")
            session.asm_mode = modes[(modes.index(session.asm_mode)+1) % len(modes)]
            focus = 0 if session.asm_mode == "source" else 6
        elif focus == 6 and key in ("i", "I", "u"):
            session.command({"i": "si", "I": "ni", "u": "patch undo"}[key])
        elif focus == 6 and assembly_rows and key in ("e", "b"):
            address = assembly_rows[assembly_cursor][0]
            if key == "b":
                session.command("breakpoint set --address 0x%x" % address)
            else:
                command = "patch 0x%x " % address
                editing = True
                history_index = len(history)
        elif focus == 6 and key in ("j", "k", curses.KEY_UP, curses.KEY_DOWN,
                                    curses.KEY_PPAGE, curses.KEY_NPAGE):
            delta = {"j": 1, "k": -1, curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                     curses.KEY_PPAGE: -10, curses.KEY_NPAGE: 10}[key]
            assembly_cursor = max(0, min(max(0, len(assembly_rows)-1), assembly_cursor+delta))
        elif focus == 1 and session.stopped() and key in (
                "j", "k", "h", "l", "J", "K", curses.KEY_UP, curses.KEY_DOWN,
                curses.KEY_LEFT, curses.KEY_RIGHT, curses.KEY_PPAGE, curses.KEY_NPAGE):
            if key in ("l", curses.KEY_RIGHT):
                locals_tree.expand()
            elif key in ("h", curses.KEY_LEFT):
                locals_tree.collapse()
            elif key == "J":
                locals_tree.collapse_all()
            elif key == "K":
                locals_tree.restore()
            else:
                delta = {"j": 1, "k": -1, curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                         curses.KEY_PPAGE: -10, curses.KEY_NPAGE: 10}[key]
                locals_tree.move(delta)
        elif key in ("h", "l", curses.KEY_LEFT, curses.KEY_RIGHT):
            delta = -1 if key in ("h", curses.KEY_LEFT) else 1
            horizontal_offsets[focus] = max(0, horizontal_offsets[focus] + delta)
        elif key in ("j", "k", curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE):
            delta = {curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                     "k": -1, "j": 1, curses.KEY_PPAGE: -10, curses.KEY_NPAGE: 10}[key]
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
                                            "Use mladbg ./app -- program arguments. "
                                            "In the TUI, F1/? opens help; :help frames explains stack navigation.")
    parser.add_argument("--batch", action="store_true", help="run commands without the TUI")
    parser.add_argument("--no-colors", action="store_true", help="use a monochrome terminal UI")
    parser.add_argument("--no-glyphs", action="store_true", help="use ASCII borders and escape non-ASCII display text")
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
            curses.wrapper(tui, session, not options.no_colors, not options.no_glyphs)
        return 0
    except (RuntimeError, OSError, KeyboardInterrupt) as error:
        print("mladbg: " + str(error), file=sys.stderr)
        return 1
    finally:
        if session:
            session.close()


if __name__ == "__main__":
    sys.exit(main())
