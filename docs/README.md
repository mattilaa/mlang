# Mlang Builtins Reference

This reference documents the built-in runtime functions used for threading,
mutexes, and atomic operations in Mlang.

It also includes language-level builtins such as `Option`, `Result`,
constructors (`Some`, `None`, `Ok`, `Err`), `match`, builtin macros, and
Rust-like attributes.

MLang also supports a practical functional-programming subset:
- immutable `let` bindings
- pipe operator `|>`
- closures
- `Option<T>` / `Result<T, E>`
- `match`
- tuple literals/types
- fold expressions over lists

For language-level exceptions and the stdlib payload type, see:
- `stdlib/std/exceptions.mla`
  (`Exception`, `new`, `with_line`, `free`, plus `throw` and `try/catch`
   syntax documented in `docs/language_syntax.md`).

For functional pipe syntax, see:
- `docs/language_syntax.md`
  (section `Pipe Operator: |>`).

String allocation intrinsics are also available:
- `String::new()`
- `String::with_capacity(cap)`
- `String::free(buf)`

`String` here is a compiler wrapper namespace over the builtin `str8` type
(not a separate type).

For higher-level str8 helpers and UTF conversions, see:
- `stdlib/std/strbuf.mla`
  (`len`, `is_empty`, `clone`, `eq`, `compare`, `starts_with`, `ends_with`,
   `contains`, `find`, `rfind`, `concat`, `repeat`,
   `to_utf16`, `from_utf16`, `to_utf8`, `free_utf16`)

For sed-style literal string replacement, see:
- `stdlib/std/sed.mla`
  (`replace_first`, `replace_all`, `substitute`).

For C++20-style span/view aliases, see:
- `stdlib/std/span.mla`
  (`Span<T>` / `span<T>` as safe non-owning aliases over `list<T>` with the
   same `sizeof` and bounds-check behavior, including `static_assert!(sizeof(expr) == ...)`).

For logical bit helpers, see:
- `stdlib/std/bits.mla`
  (`ON`, `OFF` for the builtin `bit` type).

For packed one-bit storage, see:
- `stdlib/std/bitset.mla`
  (`BitSet::new`, `len`, `resize`, `set`, `get`, `push`, `pop`, `count_ones`).
  This is the packed alternative to `list<bool>`, which is not bit-packed.

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

For pseudo-random values, see:
- `stdlib/std/rand.mla`
  (`seed`, `seed_auto`, `next_u64`, `next_i64`, `range_i64`, `next_f64`, `range_f64`).

For POSIX regular expressions, see:
- `stdlib/std/regex.mla`
  (`Regex::compile/is_match/find_start/find_end/match_start/match_end/close`,
   plus `last_error` for backend diagnostics).

For child process integration, see:
- `stdlib/std/process.mla`
  (`spawn`, `spawn_inherit`, child stdin/stdout/stderr pipes,
   `wait`, `try_wait`, `read_nonblocking`,
   `ExitStatus::success/code/signaled`).

For timing helpers, see:
- `stdlib/std/time.mla`
  (`now_ms`, `now_ns`, `sleep_ms`, `Timer::after/reset/wait/elapsed`).

For synchronization primitives, see:
- `stdlib/std/sync.mla`
  (`Mutex`, `Condvar`, `Channel` with send/post/recv/try_recv/close,
   plus lock-free SPSC `LockFreeQueue` with try_send/try_recv).

For filesystem helpers, see:
- `stdlib/std/fs.mla`
  (`File::open/create/close/write/write_line`,
   `BufReader::new/with_capacity/read_line/lines`, `free_lines`).

For thread and atomic helpers, see:
- `stdlib/std/thread.mla`
  (`join`, `mutex_new/lock/unlock/free`,
   `atomic_new/load/store/add/free`).

For terminal ANSI escape helpers, see:
- `stdlib/std/esc.mla`
  (named-value `fg/bg/reset`, style toggles, and cursor control helpers;
   auto-disabled when ANSI is unsupported, plus ACS box-drawing and
   alternate-screen helpers for TUI apps).

For terminal capabilities/termios helpers, see:
- `stdlib/std/term.mla`
  (`isatty` checks, terminal size, color level/truecolor detection,
   `supports_ansi()` convenience check, and stdin raw mode helpers).

For reusable TUI safety/cleanup patterns (raw mode + alt screen + terminal
restore guard), see:
- `docs/stdlib_mlang_api.md` section `std::esc` -> `TUI Safety Helper`.

The runtime builtins are implemented by the compiler backend and map to
platform facilities (pthreads and libc). These symbols are available without
explicit imports.

For Vec (dynamic array) helpers, see:
- `stdlib/std/vec.mla`
  (`Vec::new`, `vec![...]`, `push/pop/clear`, `len/is_empty`,
   `contains/index_of`, `sort/sort_desc/reverse/dedup`, `first/last`).

For C++-style unordered/vector wrapper types, see:
- `stdlib/std/unordered.mla`
  (`HashMapI64I64`, `QuickMapI64I64`, `QuickMapVecI64I64`, `HashSetI64`, `UnorderedMap<K,V>`, `UnorderedSet<T>`,
   `Vector<T>`).

For benchmark anti-optimization helpers, see:
- `stdlib/std/bench.mla`
  (`do_not_optimize_i64`, `do_not_optimize_i32`, `clobber_memory`).

## Language Highlights

Recent language features are documented in:
- `docs/language_syntax.md`

Includes:
- `use type` aliases (including generic aliases) and block-scoped shadowing
- alias-overlap diagnostics with `file:row:column` locations
- `f32` / `f64` primitives
- inline assembly via `asm(...)`, `asm volatile(...)`, and arch-qualified forms
  such as `asm x64(...)` / `asm aarch64(...)`
- modern `if/else if/else` block syntax without mandatory `:`
- block-style `switch` statements with direct `case value: { ... }` syntax
- guarded `if` forms, including `if let ... = ...: guard` and
  `if let ... == ...: guard`
- empty-block warnings
- `fn main() {}` defaulting to `fn main() -> i32 {}`
- function return type inference for non-extern functions that omit `-> Type`
- `throw` and `try/catch` exception handling with cleanup-aware unwinding
- typed lambdas and fold expressions (`|x: T| { ... }`, `(... + xs)`, `(xs * ...)`)

## Branch Feature Additions

Recent branch-level additions reflected in these docs:
- Benchmark runner mode for `#[test]` (`mlang bench`, warmup/iteration flags)
- `std::bench` anti-optimization helpers for benchmark code
- `use type` aliases with generic + block scope support and overlap diagnostics
- Guarded `if/else if` and guarded `while` forms; optional plain-colon warnings
- `switch` / `case` statements for integers, strings, booleans, enums, and
  other `==`-comparable values
- functional pipe operator `|>` for left-to-right call composition
- builtin `bit` values plus `sizeof(Type)` / `sizeof(expr)` in runtime code and `static_assert!`
- `fn main() {}` defaulting to `-> i32`, plus function return-type inference
- `f32`/`f64` primitive names
- Inline assembly with `volatile` and target-arch qualifiers (`x86`, `x64`, `aarch64`)
- Lambda and fold expressions with example coverage
- Functional-programming style examples using closures, `Option`, `Result`,
  `match`, and folds
- Exception handling with `throw`, `try/catch`, and `std::exceptions::Exception`
- Enum backing types (`: i64`, `: u8`), range checks, and compatibility checks
- Terminal helpers (`std::term`, `std::esc`) including ANSI auto-disable on non-TTY
- Regex module (`std::regex`)
- Hash-based unordered containers + quickmap variants (`std::unordered`)
- GoogleTest-like test expectation helpers (`std::testing`)
- MLang package-manager frontend updates including optional Ninja build flag
- CLI parity coverage (`mlang-frontend-mla`) for compile/test arguments, trailing `--tests` handling, and Robot sub-suites (`MLang Frontend Trailing Tests *`, `MLang Frontend CompileOnly TestsFlag *`).
- pkg frontend caching coverage (`MLang Frontend Pkg Mla Mode Reuses Cached Frontend Binary`) to prevent redundant pkg frontend recompiles when cache keys/source/backend are unchanged.

Related example files:
- `examples/lambda_fold_demo.mla`
- `examples/lambda_fold_patterns.mla`
- `examples/lambda_fold_advanced.mla`
- `examples/functional_option_result_demo.mla`
- `examples/functional_closure_fold_demo.mla`
- `examples/pipe_operator_demo.mla`

## Networking + Robot Example

For the multithreaded TCP server/client examples and local Robot run commands,
see the root project `README.md` section:
- `Multithreaded TCP Demo (Local)`
- `Advanced Protocol Stack Demo (Local)` for isolated subdirectory examples:
  - `examples/protocol_mt/server.mla`
  - `examples/protocol_mt/client.mla`
  - `examples/protocol_mt/run_demo.sh`

See also:
- `docs/ownership_model.h` for the phase-1 ownership model (`Copy` vs
  `move-only`) that borrow checking builds on.
- `docs/stdlib_mlang_api.md` for Mlang-level stdlib module APIs
  (`std::bench`, `std::esc`, `std::fs`, `std::io`, `std::json`, `std::math`, `std::net`,
  `std::process`, `std::rand`, `std::regex`, `std::strbuf`, `std::sync`, `std::term`,
  `std::exceptions`,
  `std::testing`,
  `std::thread`, `std::time`, `std::unordered`, `std::vec`).
- `docs/language_syntax.md` for up-to-date language syntax and diagnostics.
- `docs/language_attributes.md` for Rust-like attributes such as
  `#[derive(Debug)]` and `#[test]`.
