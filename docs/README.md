# Mlang Builtins Reference

This reference documents the built-in runtime functions used for threading,
mutexes, and atomic operations in Mlang.

It also includes language-level builtins such as `Option`, `Result`,
constructors (`Some`, `None`, `Ok`, `Err`), `match`, builtin macros, and
Rust-like attributes.

String allocation intrinsics are also available:
- `String::new()`
- `String::with_capacity(cap)`
- `String::free(buf)`

For higher-level string helpers and UTF conversions, see:
- `stdlib/std/strbuf.mla`
  (`len`, `is_empty`, `clone`, `eq`, `compare`, `starts_with`, `ends_with`,
   `contains`, `find`, `rfind`, `concat`, `repeat`,
   `to_utf16`, `from_utf16`, `to_utf8`, `free_utf16`)

For std-style IO helpers with synchronized stdout, see:
- `stdlib/std/io.mla`
  (`stdin()`, `stdout()`, `lock(stdout())`, `unlock(lockToken)`,
   `write_sync(stdout(), ...)`, `writeln_sync(stdout(), ...)`).
  Also includes scope-exit destructor support for `StdoutLock`
  via `__drop(lockToken: StdoutLock)`.

The runtime builtins are implemented by the compiler backend and map to
platform facilities (pthreads and libc). These symbols are available without
explicit imports.

See also:
- `docs/language_reference.h` for Doxygen groups covering language builtins
  (types, keywords, macros, attributes).
- `docs/language_attributes.md` for Rust-like attributes such as
  `#[derive(Debug)]` and `#[test]`.
- `docs/language_attributes.mlastub` for syntax-highlighted, LSP-indexed
  attribute examples used by go-to-definition.
