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

Then start either native debugger:

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
  `str8` as a C string, so the debugger prints its text; `ptr<T>` and
  references as pointers.
- Structs with their fields (`p p.x`), a method's `self` as a pointer to its
  struct (`p self->x`), enums with their variants (shown by name), and
  `list<T>` as `{ len, data }` with `data` typed (`p numbers.data[1]`). Tuples,
  maps and other aggregates show numbered fields.
- `{ }` blocks as lexical scopes; code from imported modules at its own `.mla`
  file.

## Limits

- Function symbols keep MLang's mangled names (`scale__i32_i32`; methods as
  `Point_sum`), so breakpoints by name use those; `file:line` breakpoints need
  no names.
- Closures and compiler-generated wrappers carry no debug info.
- DWARF has no language code for MLang; the compile unit is marked as C, so
  `p`/`print` and `expression` use C syntax on MLang values.
- With optimization (`-O1` and up) values may live in registers or be gone;
  use `-g -O0` to see every variable at every line.

An explicit optimization level is respected, for example `mlang -g -O2
main.mla -o app`. `--debug` remains the separate verbose/debug-print option; it
does not enable DWARF output.
