# Debugging native MLang programs

Compile an executable with `-g` (or `--debug-info`) to emit DWARF debug info:
a line table, a function for every MLang function and method, and the
parameters, local variables and loop variables with their types. Unless an
optimization level is explicitly selected, the compiler uses `-Og`, which keeps
most variables visible but may hold some (loop counters, say) in registers only
part of the time. `-g -O0` keeps every variable in its stack slot at each
source line: it also generates unoptimized machine code. On macOS the
executable gets a `.dSYM` bundle (made with `dsymutil`) next to it.

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
The bootstrap `mlang-config` Install menu includes an **Install mladbg** option,
enabled by default. For scripted configuration, use `mlang-config --mladbg off
--write` to disable it or `--mladbg on --write` to enable it; `build.sh --install`
then uses the saved selection. Existing
configuration files without this preference default to enabling the debugger.
On macOS, Xcode's command-line tools supply them. On Linux, distribution
packages commonly provide `lldb`, `python3-lldb`, and `python3`.
`MLADBG_PYTHON` selects a Python interpreter matching the installed bindings;
`MLADBG_LLDB` selects the `lldb` executable used to locate those bindings.

The TUI displays source, locals and arguments, stack frames, breakpoints, and
program output. Press `:` to enter commands and `Tab` to select a pane. Use
`j`/`k` or Up/Down to scroll vertically, `h`/`l` or Left/Right to scroll
horizontally, and Page Up/Down to scroll a page. Vim navigation applies outside
command entry. Resize the terminal to at least 70 columns by 18 rows.

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
MLang expressions are not implemented.

Prefer `-g -O0` when inspecting variables. Scalar locals, parameters, structs,
strings, pointers, enums, and lists have DWARF descriptions; see the type
coverage below. Optimized code may remove variables or move statements. At
function entry, step past initialization before inspecting local storage.

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
break main.mla:12
run
info locals
print count * 2
bt
next
```

Or with LLDB:

```sh
lldb ./app
```

```lldb
b main.mla:12
run
frame variable
p count * 2
bt
next
```

## What is described

- Integers (with their signedness), `f32`/`f64` and `bool` as base types;
  `str8` as a C string and UTF-16 buffers as `str16`, so the debugger prints
  their text. `ptr<T>` carries its pointee type; references describe the local
  value storage, including copy-in/copy-out parameters.
- Structs with their fields (`p p.x`), a method's `self` as a pointer to its
  struct (`p self->x`), enums with their variants (shown by name), and
  `list<T>` and `array<T, N>` as `{ len, data }` with `data` typed
  (`p numbers.data[1]`). Maps expose `{ len, keys, values }` with typed pointers
  (`p mapping.values[0].x`). Tuples expose typed `_0`, `_1`, etc. fields
  (`p pair._0`). Nested, generic and inherited structs retain field layouts,
  including packed `bit` fields. Inferred collection declarations retain
  semantic element types.
- `{ }` blocks as lexical scopes; code from imported modules at its own `.mla`
  file.

The `mladbg` locals pane and `p VARIABLE` expand structs and tuples, display
collection lengths, and preview list/array elements and map entries. Previews
are limited to 16 elements, 3 levels of nesting, and 128 values per variable.
Unreadable memory, negative lengths, and null data pointers show an unavailable
or invalid-value message. Pointers are not followed automatically: use
`p *pointer` to inspect a pointee explicitly. Previews read memory without
calling functions in the debuggee. The same display is used in batch mode.

## Limits

- Function symbols keep MLang's mangled names (`scale__i32_i32`; methods as
  `Point_sum`), so breakpoints by name use those; `file:line` breakpoints need
  no names.
- Closures and compiler-generated wrappers carry no debug info.
- Globals and arbitrary MLang expressions are not described. String-backed
  enums display their underlying text; numeric enums display variant names.
- `str16` inspection expects a UTF-16 buffer (for example from `to_utf16`);
  a UTF-8 string literal assigned directly to `str16` still has UTF-8 storage.
- DWARF has no language code for MLang; the compile unit is marked as C, so
  `p`/`print` and `expression` use C syntax on MLang values.
- With optimization (`-O1` and up) values may live in registers or be gone;
  use `-g -O0` to see every variable at every line.

An explicit optimization level is respected, for example `mlang -g -O2
main.mla -o app`. `--debug` remains the separate verbose/debug-print option; it
does not enable DWARF output.
