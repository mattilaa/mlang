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
  (`expect_true`, `expect_false`, `expect_eq`, `expect_not_eq`,
  `expect_array_eq`, `expect_array_not_eq`; and fatal
  `verify_eq`, `verify_not_eq`, `verify_array_eq`, `verify_array_not_eq`) with non-fatal
  failure counting via `reset/checks/failures/result`, plus lightweight mocks
  (`Mock`, `mock_new`, `mock_expect_call`, `mock_called`, `mock_verify`).
  Use `expect_*` when a test should continue after a failed check (non-fatal),
  and `verify_*` when a failed check should abort the test immediately (fatal).
- `std/thread.mla`: thread/concurrency helpers (join/mutex/atomic wrappers).
- `std/io.mla`: stdin/stdout helpers with synchronization support
  (`stdin()`, `stdout()`, `lock(stdout())`, `write_sync`, `writeln_sync`)
  and scope-exit destructor for `StdoutLock`.
  Also includes stderr writes/flush, stream buffering controls
  (`set_stdin_buffering`, `set_stdout_buffering`, `set_stderr_buffering`)
  and non-blocking stdin reads (`read_line_nonblocking`,
  `read_keys_nonblocking`, `read_key_nonblocking`).
- `std/strbuf.mla`: string allocation + utility helpers
  (`String::new/with_capacity/free`, len/compare/find/rfind/repeat,
   `pad_left/pad_right`, numeric padding helpers
   `pad_i64_left/pad_i64_right/zpad_i64`, UTF-8<->UTF-16 converters and helpers).
- `std/bytes.mla`: dynamic raw byte buffer helpers for protocol/binary payloads
  (reserve/append/index/set, binary-safe `to_hex`, text-oriented `to_string`).
- `std/hash.mla`: stable 64-bit hashing helpers
  (`hash_str`, `hash_str16`, `hash_i64`, `hash_bool`, `combine`, `to_hex`,
   and incremental `Hasher`).
- `std/gps.mla`: latitude/longitude helpers for routing and TSP-style code
  (`point`, `deg_to_rad`, `distance_m`, `project_points`, `project_xs`,
   `project_ys`).
- `std/platform.mla`: multiplatform detection helpers built on
  builtin macros (`windows!()`, `posix!()`, `linux!()`, `macos!()`,
  `x64!()`, `aarch64!()`).
- `std/serde.mla`: binary serialization/deserialization helpers with
  growable output buffer and sequential reader
  (`Binary::write_*`, `Reader::read_*`, file roundtrip helpers, and
  `BinarySerde` trait for custom types).
- `std/protocol.mla`: base framed TCP protocol transport helpers intended for
  project-level derived protocol stacks (`opcode + payload` framing).
- `std/json.mla`: JSON parse/stringify + object/array navigation and iterators
  (`JsonDoc::parse/from_file`, `JsonDoc::stringify`,
   `JsonValue::get/index/as_*`, `iter_array`, `iter_object`),
  backed by RapidJSON.
- `std/net.mla`: TCP client/server over libc sockets
  (`TcpListener::bind/accept/local_port`, `TcpStream::connect/read/write`,
   `set_nonblocking`, `set_read_timeout_ms`, `set_write_timeout_ms`,
   `try_clone`, `from_handle`/`raw_handle` for multithread handoff, and
   listener backlog tuning via `set_backlog`).
- `std/ssl.mla`: TLS client/server streams over OpenSSL
  (`TlsStream::connect/connect_with_options/read/write/close`,
   `TlsListener::bind/accept/local_port`, and raw-handle handoff helpers for
   multithreaded local TLS servers).
- `std/regex.mla`: POSIX regular expression helpers
  (`Regex::compile/is_match/find_start/find_end/match_start/match_end/close`)
  with error retrieval via `std::regex::last_error()`.
- `std/algorithm.mla`: algorithm namespace root with submodules:
  - `std::algorithm::fuzzy`: subsequence fuzzy matching/ranking
    (`is_match`, `score`, `filter_indices`, `best_index`)
  - `std::algorithm::order`: ordered-sequence helpers for `list<i64>`
    (`sort_i64`, `reverse_i64`, `is_sorted_i64`, `unique_sorted_i64`,
     `lower_bound_i64`, `upper_bound_i64`, `binary_search_i64`)
  - `std::algorithm::numeric`: numeric helpers for `list<i64>`
    (`accumulate_i64`, `partial_sum_i64`, `adjacent_difference_i64`,
     `inner_product_i64`)
  - `std::algorithm::ranges`: C++20 ranges-style helpers for `list<i64>`
    (`unique_i64`, `unique_stable_i64`, `find_i64`, `count_i64`,
     `contains_i64`, `remove_i64`, `take_i64`, `drop_i64`)
  - `std::algorithm::rand`: random helpers mirroring `std::rand`
  - `std::algorithm::regex`: regex helpers mirroring `std::regex`
  - `std::algorithm::fft`: FFT helpers for forward/inverse transforms
  - `std::algorithm::genetic`: GA helpers for genome/population evolution
- `std/esc.mla`: ANSI terminal escape helpers with named color/cursor values
  cursor control (`fg/bg/reset`, `bold_on/off`, `underline_on/off`,
  `cursor`, `cursor_move`), auto-disabled when `std::term::supports_ansi()`
  is false.
- `std/term.mla`: terminal capability + termios helpers
  (`stdin/stdout/stderr` tty detection, size, color level/truecolor, and
  stdin raw mode enable/restore, plus `supports_ansi()` convenience check).
- `std/image.mla`: image probing and terminal image rendering helpers
  (`probe`, `render_truecolor`, `render_truecolor_with_mode`) for converting
  decoded images into truecolor terminal glyph output with a caller-selected
  terminal cell resolution. Includes block, density, edge-aware ASCII, ASCII
  ramp, quadrant, and braille-oriented glyph modes for different detail levels
  and terminal compatibility tradeoffs.
- `std/fs.mla`: filesystem helpers including current directory inspection and
  mutation (`cwd`, `parent_dir`, `mkdir_p`, `remove_tree`, `chdir`), file I/O,
  buffered line reading, recursive globbing, and whole-file text helpers.
- `std/env.mla`: runtime environment helpers including argv access, help text
  rendering, process cwd, stdout/stderr printing, and environment variable
  lookup via `get(name)`.
- `std/unordered.mla`: hash-backed unordered containers
  (`HashMapI64I64`, `QuickMapI64I64`, `QuickMapVecI64I64`, `HashSetI64`) plus C++-style wrappers
  (`UnorderedMap<K,V>`, `UnorderedSet<T>`, `Vector<T>`).
