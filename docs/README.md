# Mlang Builtins Reference

This reference documents the built-in runtime functions used for threading,
mutexes, and atomic operations in Mlang.

It also includes language-level builtins such as `Option`, `Result`,
constructors (`Some`, `None`, `Ok`, `Err`), `match`, builtin macros, and
Rust-like attributes.

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
