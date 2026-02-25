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
  Extended APIs include stderr support, explicit stream flushing,
  buffering controls (`set_*_buffering`), and non-blocking stdin polling
  (`read_line_nonblocking`).

For JSON parsing/stringify/navigation and iterators, see:
- `stdlib/std/json.mla`
  (`JsonDoc::parse/from_file`, `JsonDoc::stringify(_pretty)`,
   `JsonValue::get/index/as_*`, `iter_array`, `iter_object`).

For numeric helpers, see:
- `stdlib/std/math.mla`
  (`add/subtract/multiply/square/abs/min/max/clamp/pow/sqrt/sin/cos/tan/`
   `floor/ceil/round/log/exp/modulo`, plus `sum_range`, `factorial`).

For TCP networking, see:
- `stdlib/std/net.mla`
  (`TcpListener::bind/accept/local_port`, `TcpStream::connect/read/write`,
  `set_nonblocking`, `set_read_timeout_ms`, `set_write_timeout_ms`).

For child process integration, see:
- `stdlib/std/process.mla`
  (`spawn`, `spawn_inherit`, child stdin/stdout/stderr pipes,
   `wait`, `ExitStatus::success/code/signaled`).

For timing helpers, see:
- `stdlib/std/time.mla`
  (`now_ms`, `now_ns`, `sleep_ms`, `Timer::after/reset/wait/elapsed`).

For synchronization primitives, see:
- `stdlib/std/sync.mla`
  (`Mutex`, `Condvar`, `Channel` with send/recv/try_recv/close).

For filesystem helpers, see:
- `stdlib/std/fs.mla`
  (`File::open/create/close/write/write_line`,
   `BufReader::new/with_capacity/read_line/lines`, `free_lines`).

For thread and atomic helpers, see:
- `stdlib/std/thread.mla`
  (`join`, `mutex_new/lock/unlock/free`,
   `atomic_new/load/store/add/free`).

The runtime builtins are implemented by the compiler backend and map to
platform facilities (pthreads and libc). These symbols are available without
explicit imports.

For Vec (dynamic array) helpers, see:
- `stdlib/std/vec.mla`
  (`Vec::new`, `vec![...]`, `push/pop/clear`, `len/is_empty`,
   `contains/index_of`, `sort/sort_desc/reverse/dedup`, `first/last`).

## Language Highlights

Recent language features are documented in:
- `docs/language_syntax.md`

Includes:
- `use type` aliases (including generic aliases) and block-scoped shadowing
- alias-overlap diagnostics with `file:row:column` locations
- `f32` / `f64` primitives and `float` / `double` aliases
- modern `if/else if/else` block syntax without mandatory `:`
- guarded `if` forms, including `if let ... = ...: guard` and
  `if let ... == ...: guard`
- empty-block warnings
- `fn main() {}` defaulting to `fn main() -> i32 {}`

## Networking + Robot Example

For the multithreaded TCP server/client examples and local Robot run commands,
see the root project `README.md` section:
- `Multithreaded TCP Demo (Local)`

See also:
- `docs/ownership_model.h` for the phase-1 ownership model (`Copy` vs
  `move-only`) that borrow checking builds on.
- `docs/stdlib_mlang_api.md` for Mlang-level stdlib module APIs
  (`std::fs`, `std::io`, `std::json`, `std::math`, `std::net`, `std::process`, `std::strbuf`, `std::sync`, `std::thread`, `std::time`, `std::vec`).
- `docs/language_syntax.md` for up-to-date language syntax and diagnostics.
- `docs/language_attributes.md` for Rust-like attributes such as
  `#[derive(Debug)]` and `#[test]`.
