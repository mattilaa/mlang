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
- `std/bench.mla`: benchmark helpers for anti-optimization barriers
  (`do_not_optimize_i64`, `do_not_optimize_i32`, `clobber_memory`).
- `std/testing.mla`: GoogleTest-like expectation helpers
  (`expect_true`, `expect_false`, `expect_eq`, `EXPECT_*`) with non-fatal
  failure counting via `reset/checks/failures/result`.
- `std/thread.mla`: thread/concurrency helpers (join/mutex/atomic wrappers).
- `std/io.mla`: stdin/stdout helpers with synchronization support
  (`stdin()`, `stdout()`, `lock(stdout())`, `write_sync`, `writeln_sync`)
  and scope-exit destructor for `StdoutLock`.
  Also includes stderr writes/flush, stream buffering controls
  (`set_stdin_buffering`, `set_stdout_buffering`, `set_stderr_buffering`)
  and non-blocking stdin reads (`read_line_nonblocking`).
- `std/strbuf.mla`: string allocation + utility helpers
  (`String::new/with_capacity/free`, len/compare/find/rfind/repeat,
   UTF-8<->UTF-16 converters and helpers).
- `std/json.mla`: JSON parse/stringify + object/array navigation and iterators
  (`JsonDoc::parse/from_file`, `JsonDoc::stringify`,
   `JsonValue::get/index/as_*`, `iter_array`, `iter_object`),
  backed by RapidJSON.
- `std/net.mla`: TCP client/server over libc sockets
  (`TcpListener::bind/accept/local_port`, `TcpStream::connect/read/write`,
   `set_nonblocking`, `set_read_timeout_ms`, `set_write_timeout_ms`,
   `try_clone`, `from_handle`/`raw_handle` for multithread handoff, and
   listener backlog tuning via `set_backlog`).
- `std/regex.mla`: POSIX regular expression helpers
  (`Regex::compile/is_match/find_start/find_end/match_start/match_end/close`)
  with error retrieval via `std::regex::last_error()`.
- `std/esc.mla`: ANSI terminal escape helpers with named color/cursor values
  cursor control (`fg/bg/reset`, `bold_on/off`, `underline_on/off`,
  `cursor`, `cursor_move`), auto-disabled when `std::term::supports_ansi()`
  is false.
- `std/term.mla`: terminal capability + termios helpers
  (`stdin/stdout/stderr` tty detection, size, color level/truecolor, and
   stdin raw mode enable/restore, plus `supports_ansi()` convenience check).
- `std/unordered.mla`: hash-backed unordered containers
  (`HashMapI64I64`, `QuickMapI64I64`, `QuickMapVecI64I64`, `HashSetI64`) plus C++-style wrappers
  (`UnorderedMap<K,V>`, `UnorderedSet<T>`, `Vector<T>`).
