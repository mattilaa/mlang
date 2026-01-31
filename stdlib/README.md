# MLang stdlib (editor-only)

This directory contains documentation-only modules used by editor tooling
(e.g., go-to-definition in uvim). These files are installed to the user
prefix on `make install` and are not required at runtime.

Currently provided:
- `types.mla`: built-in primitive and generic types.
- `macros.mla`: built-in macros (println!, format!, assert_eq!, ...).
- `test.mla`: test framework helpers (test::assert, test::run_all).
