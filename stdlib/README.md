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
- `std/io.mla`: stdin/stdout helpers with synchronization support
  (`stdin()`, `stdout()`, `lock(stdout())`, `write_sync`, `writeln_sync`)
  and scope-exit destructor for `StdoutLock`.
- `std/strbuf.mla`: string allocation + utility helpers
  (`String::new/with_capacity/free`, len/compare/find/rfind/repeat,
   UTF-8<->UTF-16 converters and helpers).
- `std/json.mla`: JSON parse/stringify + object/array navigation and iterators
  (`JsonDoc::parse/from_file`, `JsonDoc::stringify`,
   `JsonValue::get/index/as_*`, `iter_array`, `iter_object`),
  backed by RapidJSON.
