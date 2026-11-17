The [logging demo](../variadic_logging.mla) accepts mixed argument types:

```mla
log("INFO", "connected", "client", 42, "ready", true);
log("DEBUG", "gain", 1.5);
log("INFO");
```

Output uses MLang's normal `format!` conventions (booleans are `1`/`0` and
floating-point values have six decimal places):

```text
[INFO] connected client 42 ready 1
[DEBUG] gain 1.500000
[INFO]
```

The [logger](logger.mla) forwards `args...` from `log` to `format_record` and
then to `collect`. The comma fold `(collect_one(&mut parts, args), ...)`
formats each argument using its concrete type and appends it to a
`list<str8>` in argument order. No C varargs or runtime type tags are involved.
Each argument must support `format!("{}", value)`.

From the repository root:

```sh
build/mlang examples/variadic_logging.mla -o /tmp/variadic_logging
/tmp/variadic_logging
build/mlang --tests examples/variadic_logging_tests.mla
build/mlang --tests tests/variadic_generic_tests.mla
```

The logging tests use the same module as the demo. They check mixed types,
exact output, empty and single-element packs, repeated specializations,
append order, and evaluation of arguments exactly once from left to right.
