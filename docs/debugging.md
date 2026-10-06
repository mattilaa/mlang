# Debugging native MLang programs

Compile an executable with `-g` (or `--debug-info`) to emit DWARF function and
source-line information. Unless an optimization level is explicitly selected,
the compiler uses `-Og` for a more useful debugging experience:

```sh
mlang -g main.mla -o app
```

Then start either supported native debugger:

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
Local-variable inspection is not yet described in DWARF. `--debug` remains the
separate verbose/debug-print option; it does not enable DWARF output.
