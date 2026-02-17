# MLang stdlib (editor-only)

This directory contains stdlib modules used by editor tooling
(e.g., go-to-definition in uvim) and by the compiler at build time.
These files are installed to the user prefix on `make install`.

Currently provided:
- `types.mla`: built-in primitive and generic types.
- `macros.mla`: built-in macros (println!, format!, assert_eq!, ...).
- `attributes.mla`: built-in attributes (`#[derive(Debug)]`, `#[test]`).
- `test.mla`: test framework helpers (test::assert, test::run_all).

- `std/math.mla`: generic math helpers (backed by libmlang_std).
- `std/thread.mla`: thread/concurrency helpers (join/mutex/atomic wrappers).
