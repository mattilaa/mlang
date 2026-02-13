# Mlang Runtime Builtins

This reference documents the built-in runtime functions used for threading,
mutexes, and atomic operations in Mlang.

The runtime builtins are implemented by the compiler backend and map to
platform facilities (pthreads and libc). These symbols are available without
explicit imports.

See also:
- `docs/language_attributes.md` for Rust-like attributes such as
  `#[derive(Debug)]` and `#[test]`.
- `docs/language_attributes.mlastub` for syntax-highlighted, LSP-indexed
  attribute examples used by go-to-definition.
