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

Colors are enabled when the terminal supports them: keywords/types, strings,
and numbers have distinct colors, and the execution line uses a blue
background. Launch with `mladbg --no-colors ./app` for a monochrome display.

Panes, help and file-completion dropdowns use UTF-8 box-drawing glyphs.
Use `mladbg --no-glyphs ./app` for ASCII `+`, `-`, `|` borders. Non-UTF-8
terminal encodings automatically fall back to ASCII. In this mode, non-ASCII
source, filenames and program output are escaped for display; commands and
actual filesystem paths are unchanged. Combine `--no-glyphs --no-colors` for
a plain ASCII monochrome UI.

Locals are a tree with structures and collections collapsed initially. Focus
the pane with Tab; `j`/Down selects the next row, `k`/Up selects the previous
row, `l`/Right expands it, and `h`/Left collapses it. On an already-open node,
`l` enters its first child; on a closed child, collapse returns to the parent.
Moving with `j`/`k` never changes expansions. Shift-J collapses all;
Shift-K restores the previous expansions and selection. These keys apply only
to the locals pane outside command entry.
Expansion state is preserved separately for each selected stack frame.

### Assembly and live instruction editing

Press `a` to cycle source, mixed source/assembly, and assembly-only views.
Mixed view splits the source area vertically; small terminals use assembly
alone. `:asm line` filters to the selected source line, `:asm function` shows
the selected function (bounded to 256 instructions), and `:asm off` restores
source-only viewing. Bytes, addresses and source locations are shown, with
`=>` marking the selected frame PC. Both views follow frame/thread selection.

In assembly, `j`/`k` and arrows select instructions, `h`/`l` scroll horizontally,
`b` adds an address breakpoint, and `i`/`I` step into/over a machine instruction
(`:si`/`:ni` work from any pane). `e` prefills `patch 0xADDRESS ` in the command
prompt; enter assembly or cancel with Esc. Instructions already executed are
not replayed by patching them; select frame 0 to inspect the actual next PC.

```text
patch pc nop                 # replace the current instruction with NOPs
patch 0xADDRESS ASM          # replace one instruction at a visible address
patch-bytes pc 1f 20 03 d5   # explicit bytes (this example is AArch64 NOP)
patch list                  # original and replacement bytes
patch undo                  # restore the last patch (also u in assembly)
si                          # execute one machine instruction
continue                    # resume explicitly
```

The inline comments above are explanations, not part of the commands.
Replacements must have exactly the original instruction size; multiple
instructions separated by `;` may fit that space. `nop` automatically fills
the original space on x86/AArch64.
`pc+OFFSET` / `pc-OFFSET` can address a nearby instruction without hardcoding
ASLR-dependent addresses (for example `patch pc+4 nop`).
Instruction starts are validated against the selected function, and writes
are verified. Undo checks for intervening
edits and refuses to overwrite them. History is scoped to the live process;
relaunching discards it. Patches never change the executable on disk or source
and do not automatically continue. All threads remain stopped during editing.

General assembly requires clang's integrated assembler; `MLADBG_CLANG` can
select its executable. x86 assembly uses AT&T syntax. Directives, labels and
symbolic/PC-relative relocations are rejected; use explicit resolved bytes
for such changes. No trampoline or instruction resizing is performed.
Incorrect code can crash/corrupt the inferior, and OS code-signing/W^X or
remote debugger restrictions may prevent writes. Errors are reported; failed
writes trigger a best-effort rollback. See `:help memory` for built-in help.

Press F1 or `?` for a full-screen, scrollable help pane, or enter `:help frames`
to jump directly to stack navigation. Help also covers execution, variables,
breakpoints/watchpoints, threads, memory, sessions, and keyboard controls.
Inside help, `0`–`8` or Tab select a topic, Vim/arrow/Page keys scroll, and
Esc/F1/`?`/`q` return to the debugger. Opening help does not pause execution.
`help lldb COMMAND` prints native command details to the console.

While entering `b`/`break`, `file`, `target create`, or `command source`, Tab
opens a file dropdown. `:b m` filters names starting with `m`; bare `:b` lists
the current working directory. Listings include directories and hidden files;
choose directories to browse paths beneath them, without a recursive scan.
Use `j`/`k`, Up/Down or Page Up/Down to browse, Enter/Tab/Right to choose a file
or open a directory, and Left to browse the parent. Esc closes the menu while
keeping your command; another Esc cancels command entry. Other printable keys
refine the prefix, except `j`/`k` while the menu is open (close it first to type
those letters). Selecting a file does not execute the command: append `:LINE`
for a breakpoint and press Enter. Existing line suffixes and paths with spaces
are preserved. Unreadable directories and empty matches are reported; results
are bounded to 256 entries, so narrow the prefix in very large directories.

| Action | Command | Key |
| --- | --- | --- |
| Launch | `run` or `r` | F5 |
| Continue | `continue` or `c` | F5 |
| Step over / into / out | `next`, `step`, `finish` (`n`, `s`, `f`) | F6 / F7 / F8 |
| Breakpoint | `b main`, `b add`, `b file.mla:12`, `b 12` | F9 at current line |
| Breakpoint management | `delete 1`, `disable 1`, `enable 1` | |
| Stack / frame | `bt`, `frame 1`, `up`, `down` | |
| Variables / expressions | `locals`, `p count`, `p count + 1` | |
| Threads | `threads`, `thread 2` | |
| Write watchpoint | `watch count` | |
| Registers / assembly / memory | `registers`, `disassemble`, `memory 0xADDRESS` | |
| Interrupt | `interrupt` | Ctrl-C |
| Attach / detach | `attach PID`, `detach` | |
| Quit | `quit` | q |
| Help | `help`, `help frames`, `help keys` | F1 / ? |

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

The `mladbg` locals tree reads children only when opened, with up to 16 children
per node, 8 levels, and 256 displayed rows. Pointer nodes can be opened
explicitly; null pointers remain leaves. Collection lengths are shown while
collapsed. `p VARIABLE` and `locals` in the console expand structs and tuples
and preview collection entries, limited to 16 elements, 3 levels of nesting,
and 128 values per variable.
Unreadable memory, negative lengths, and null data pointers show an unavailable
or invalid-value message. Pointers are not followed automatically: expand a
pointer row or use `p *pointer` to inspect its pointee explicitly. All previews
read memory without calling functions in the debuggee. Batch mode uses the
console format.

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
