# Debugging native MLang programs

Compile an executable with `-g` (or `--debug-info`) to emit DWARF function and
source-line information. Unless an optimization level is explicitly selected,
the compiler uses `-Og` for a more useful debugging experience:

```sh
mlang -g main.mla -o app
```

Start the MLang terminal debugger:

```sh
mladbg ./app -- program-arguments
```

`mladbg` is a native launcher for an LLDB-powered Python/curses frontend on
macOS and Linux. It is built by default (`BUILD_MLADBG=ON`) and installed with
the compiler. Install LLDB with its Python bindings and Python's curses module.
On macOS, Xcode's command-line tools supply them. On Linux, distribution
packages commonly provide `lldb`, `python3-lldb`, and `python3`.
`MLADBG_PYTHON` selects a Python interpreter matching the installed bindings;
`MLADBG_LLDB` selects the `lldb` executable used to locate those bindings.

The TUI displays source, locals and arguments, stack frames, breakpoints, and
program output. Press `:` to enter commands, `Tab` to select a pane, and arrows
or Page Up/Down to scroll. Resize the terminal to at least 70 columns by 18 rows.

| Action | Command | Key |
| --- | --- | --- |
| Launch | `run` or `r` | F5 |
| Continue | `continue` or `c` | F5 |
| Step over / into / out | `next`, `step`, `finish` (`n`, `s`, `f`) | F6 / F7 / F8 |
| Breakpoint | `b main`, `b add`, `b file.mla:12`, `b 12` | F9 at current line |
| Breakpoint management | `delete 1`, `disable 1`, `enable 1` | |
| Stack / frame | `bt`, `frame 1` | |
| Variables / expressions | `locals`, `p count`, `p count + 1` | |
| Threads | `threads`, `thread 2` | |
| Write watchpoint | `watch count` | |
| Registers / assembly / memory | `registers`, `disassemble`, `memory 0xADDRESS` | |
| Interrupt | `interrupt` | Ctrl-C |
| Attach / detach | `attach PID`, `detach` | |
| Quit | `quit` | q |

`b add` also resolves MLang overloads such as `add__i32_i32`. Unresolved
breakpoints remain pending. Commands not listed above are passed to LLDB, so
conditional breakpoints (`breakpoint modify -c 'count > 5' 1`), read watchpoints,
memory writes, source maps (`settings set target.source-map OLD NEW`), and other
LLDB commands remain available. Expressions use LLDB's C/C++ syntax; arbitrary
MLang expressions and container formatters are not implemented.

Prefer `-g -O0` when inspecting variables. Scalar locals and parameters (signed
and unsigned integers, bool/bit, f32/f64) have DWARF descriptions. Structs,
containers, pointers, globals, and lexical shadowing are not fully described
yet. Optimized code may remove variables or move statements. At function entry,
step past initialization before inspecting local storage.

Attach directly with `mladbg --attach PID`. Quitting detaches an attached
process and terminates a process launched by the debugger. Inferior stdin
defaults to `/dev/null` so it cannot consume TUI keystrokes; use LLDB's process
launch redirection options when debugging a program requiring input. LLDB
initialization files are not loaded automatically.

Batch mode uses the same command engine without a terminal. Options precede
the executable; arguments after it are passed to the debuggee:

```sh
mladbg --batch -ex 'b main' -ex run -ex next -ex locals -ex bt ./app
```

Build and run debugger checks:

```sh
cmake --build build --target mladbg mlang
ctest --test-dir build -L debugger --output-on-failure
# Optional live-process test (requires debugger permissions and LLDB bindings):
cmake -S . -B build -DMLADBG_INTEGRATION_TESTS=ON
ctest --test-dir build -R mladbg_integration --output-on-failure
```

You can also start either native debugger directly:

```sh
gdb ./app
```

```gdb
break main
run
next
bt
```

Or with LLDB:

```sh
lldb ./app
```

```lldb
b main
run
next
bt
```

An explicit optimization level is respected, for example `mlang -g -O0
main.mla -o app`. The compiler emits source locations for generated functions
and statements, enabling source breakpoints, stepping, and function backtraces.
Scalar locals and parameters can also be inspected directly in GDB/LLDB.
`--debug` remains the separate verbose/debug-print option; it does not enable
DWARF output.
