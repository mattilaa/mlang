# Mlang Stdlib Module API {#stdlib_mlang_api}

This page documents the **MLang-facing stdlib modules** under `stdlib/std/`.
These are the APIs you import in Mlang source via:

```mla
mod std::io;
mod std::esc;
mod std::argparser;
mod std::compiler;
mod std::date;
mod std::event_loop;
mod std::json;
mod std::jsonrpc;
mod std::bench;
mod std::math;
mod std::net;
mod std::printf;
mod std::process;
mod std::regex;
mod std::sync;
mod std::term;
mod std::time;
mod std::timer;
mod std::fs;
mod std::strbuf;
mod std::bytes;
mod std::testing;
mod std::thread;
mod std::unordered;
mod std::vec;
```

The source-of-truth implementation files are:
- `stdlib/std/fs.mla`
- `stdlib/std/esc.mla`
- `stdlib/std/argparser.mla`
- `stdlib/std/compiler.mla`
- `stdlib/std/date.mla`
- `stdlib/std/event_loop.mla`
- `stdlib/std/io.mla`
- `stdlib/std/json.mla`
- `stdlib/std/jsonrpc.mla`
- `stdlib/std/bench.mla`
- `stdlib/std/math.mla`
- `stdlib/std/net.mla`
- `stdlib/std/printf.mla`
- `stdlib/std/process.mla`
- `stdlib/std/regex.mla`
- `stdlib/std/sync.mla`
- `stdlib/std/term.mla`
- `stdlib/std/time.mla`
- `stdlib/std/timer.mla`
- `stdlib/std/strbuf.mla`
- `stdlib/std/bytes.mla`
- `stdlib/std/testing.mla`
- `stdlib/std/thread.mla`
- `stdlib/std/unordered.mla`
- `stdlib/std/vec.mla`

## Built-in Collection Methods (Compiler Intrinsics)

These methods are language/compiler intrinsics and are available directly on
collection/string values (including values typed through `use type` aliases).

### `string`
`String` in `String::new/with_capacity/free` is a compiler wrapper namespace
for this builtin type, not a distinct type declaration.

- `s.len() -> i64`
- `s.is_empty() -> int`

### `list<T>` / `Vec<T>`

`Vec<T>` is a type alias for `list<T>`, so both expose the same method surface.

- `v.len() -> i64`
- `v.is_empty() -> bool`
- `v.push(value) -> void`
- `v.pop() -> T`
- `v.clear() -> void`
- `v.contains(value) -> bool`
- `v.index_of(value) -> i64`
- `v.sort() -> void`
- `v.sort_desc() -> void`
- `v.reverse() -> void`
- `v.dedup() -> void`
- `v.first() -> T`
- `v.last() -> T`

### `map<K, V>`
- `m.len() -> i64`
- `m.keys()` iterator for `for key in m.keys() { ... }`
- `m.values()` iterator for `for val in m.values() { ... }`
- `m.entries()` iterator for `for entry in m.entries() { ... }`
  where `entry` is a tuple `(K, V)` and can be accessed via `.0` / `.1`

## Type Aliases (`use type`)

`use type` lets you introduce readable names for builtin or generic types and the alias respects lexical scope.
These declarations can appear at the top level **or** inside blocks—the alias disappears once the block exits.
Aliases also support generics, so `use type SomeMap = map<K, V>;` is equivalent to the C++ style `using`.

Example:

```mla
use type Distance = f32;
use type SomeMap = map<string, i32>;

let scores: SomeMap = {"Alice": 95, "Bob": 87};

{
    use type Distance = i32;
    let grid: Distance = 42;
    println!("grid={}", grid);
}
// outside block `Distance` still refers to `f32`
```

If an alias name is duplicated in the same scope, the compiler reports an error referencing the original definition (`file.mla:row:column: alias 'Distance' already defined`), so you can reliably locate the conflict.

## std::argparser

Module file: `stdlib/std/argparser.mla`

### Types
- `ArgParser`
- `ParseResult`

### Parser setup
- `ArgParser::new(prog: string, desc: string) -> ArgParser`
- `ArgParser::flag(self: ArgParser, long_name: string, short_name: string, help: string) -> void`
- `ArgParser::option(self: ArgParser, long_name: string, short_name: string, help: string, default_val: string) -> void`
- `ArgParser::positional(self: ArgParser, name: string, help: string) -> void`
- `ArgParser::parse(self: ArgParser, argc: i32, args: list<str8>) -> ParseResult`
- `ArgParser::print_help(self: ArgParser) -> void`
- `ArgParser::free(self: ArgParser) -> void`

### Parse results
- `ParseResult::ok(self: ParseResult) -> bool`
- `ParseResult::has_error(self: ParseResult) -> bool`
- `ParseResult::error(self: ParseResult) -> string`
- `ParseResult::help_requested(self: ParseResult) -> bool`
- `ParseResult::flag(self: ParseResult, name: string) -> bool`
- `ParseResult::get(self: ParseResult, name: string) -> string`
- `ParseResult::get_i64(self: ParseResult, name: string) -> i64`
- `ParseResult::positional(self: ParseResult, idx: i64) -> string`
- `ParseResult::positional_count(self: ParseResult) -> i64`
- `ParseResult::free(self: ParseResult) -> void`

## std::compiler

Module file: `stdlib/std/compiler.mla`

### Types
- `Session`
- `SyntaxDiagnostic`
- `DocumentSymbol`
- `DefinitionLocation`
- `ReferenceLocation`
- `ResolvedSymbol`

### Global helpers
- `session_create() -> Result<Session, string>`
- `last_status() -> i32`
- `status_name(status: i32) -> string`
- `version() -> string`
- `last_error() -> string`

### Session lifecycle and document state
- `Session::destroy(self: Session) -> Result<i32, string>`
- `Session::open_document(self: Session, uri: string, language_id: string, text: string, version: i32) -> Result<i32, string>`
- `Session::change_document(self: Session, uri: string, text: string, version: i32) -> Result<i32, string>`
- `Session::close_document(self: Session, uri: string) -> Result<i32, string>`

### Diagnostics and editor queries
- `Session::syntax_diagnostic_count(self: Session, uri: string) -> Result<i32, string>`
- `Session::syntax_diagnostic_get(self: Session, uri: string, index: i32) -> Result<SyntaxDiagnostic, string>`
- `Session::hover(self: Session, uri: string, line: i32, column: i32) -> Result<string, string>`
- `Session::completion_count(self: Session, uri: string, line: i32, column: i32) -> Result<i32, string>`
- `Session::completion_get(self: Session, uri: string, line: i32, column: i32, index: i32) -> Result<string, string>`

### Symbols, definitions, references, rename
- `Session::document_symbol_count(self: Session, uri: string) -> Result<i32, string>`
- `Session::document_symbol_get(self: Session, uri: string, index: i32) -> Result<DocumentSymbol, string>`
- `Session::definition(self: Session, uri: string, line: i32, column: i32) -> Result<DefinitionLocation, string>`
- `Session::references_count(self: Session, uri: string, line: i32, column: i32) -> Result<i32, string>`
- `Session::reference_get(self: Session, uri: string, line: i32, column: i32, index: i32) -> Result<ReferenceLocation, string>`
- `Session::resolve_symbol(self: Session, uri: string, line: i32, column: i32) -> Result<ResolvedSymbol, string>`
- `Session::rename_is_safe(self: Session, uri: string, line: i32, column: i32, new_name: string) -> Result<i32, string>`
- `Session::semantic_cache_warm(self: Session, uri: string) -> Result<i32, string>`
- `Session::semantic_cache_clear(self: Session) -> Result<i32, string>`

## std::date

Module file: `stdlib/std/date.mla`

### Types
- `DateTime`

### API
- `now() -> DateTime`
- `format_iso8601(dt: DateTime) -> string`
- `format_date(dt: DateTime) -> string`
- `format_time(dt: DateTime) -> string`

## std::event_loop

Module file: `stdlib/std/event_loop.mla`

### Types
- `EventLoop`

### API
- `EventLoop::start(queue_handle: i64, interval_ms: i64, event_name: string) -> Result<EventLoop, string>`
- `EventLoop::stop(self: EventLoop) -> i32`
- `EventLoop::close(self: EventLoop) -> i32`

## std::unordered

Module file: `stdlib/std/unordered.mla`

Hash-backed runtime containers:
- `HashMapI64I64`
  - `new()`, `close()`
  - `insert(key, value)`, `contains(key)`, `get_or(key, default_value)`
  - `remove(key)`, `len()`, `keys()`, `values()`
- `QuickMapI64I64` (convenience API over `HashMapI64I64`)
  - `new()`, `close()`
  - `set(key, value)`, `get(key, fallback)`, `has(key)`, `del(key)`
  - `len()`, `keys()`, `values()`
- `QuickMapVecI64I64` (vector-backed convenience map)
  - `new()`, `close()`
  - `set(key, value)`, `get(key, fallback)`, `has(key)`, `del(key)`
  - `len()`, `keys()`, `values()`
- `HashSetI64`
  - `new()`, `close()`
  - `insert(key)`, `contains(key)`, `remove(key)`, `len()`, `keys()`

Compatibility wrapper structs (builtin-backed):
- `UnorderedMap<K, V> { data: map<K, V> }`
- `UnorderedSet<T> { data: list<T> }`
- `Vector<T> { data: list<T> }`

## std::bench

Module file: `stdlib/std/bench.mla`

Google-benchmark style anti-optimization helpers:
- `do_not_optimize_i64(v)`
- `do_not_optimize_i32(v)`
- `clobber_memory()`

Typical benchmark usage:

```mla
mod std::bench;

#[test]
fn bench_vec_push_pop() -> i32 {
    let x: i64 = 123;
    do_not_optimize_i64(x);
    clobber_memory();
    return 0;
}
```

Executed via benchmark runner:
- `mlang bench tests`
- `mlang bench tests/bench_stdlib.mla --bench-iters 200000 --bench-warmup 20000`

## std::testing

Module file: `stdlib/std/testing.mla`

GoogleTest-like non-fatal expectation helpers:
- `expect_true(cond: bool)`
- `expect_false(cond: bool)`
- `expect_eq(expected, actual)` overloads for `i32`, `i64`, `bool`, `string`, `f32`, `f64`
- uppercase aliases: `EXPECT_TRUE`, `EXPECT_FALSE`, `EXPECT_EQ`

Fatal verify helpers (abort on failure):
- `verify_true(cond: bool)`
- `verify_false(cond: bool)`
- `verify_eq(expected, actual)` overloads for `i32`, `i64`, `bool`, `string`, `f32`, `f64`
- uppercase aliases: `VERIFY_TRUE`, `VERIFY_FALSE`, `VERIFY_EQ`

Counters/result helpers:
- `reset()`
- `checks() -> i32`
- `failures() -> i32`
- `result() -> i32` (`0` when no failures, else `1`)

Typical test usage:

```mla
mod std::testing;
use std::testing::*;

#[test]
fn test_demo() -> i32 {
    reset();
    expect_true(2 > 1);
    expect_eq("ok", "ok");
    return result();
}
```

## std::esc

Module file: `stdlib/std/esc.mla`

ANSI escape helpers for terminal color/style and cursor control. API is
value-set-driven so call sites avoid raw numeric SGR/control codes.
Sequences auto-disable to `""` when `std::term::supports_ansi()` is false.

### Value-set Types
- `Color` (alias of `i32`)
- `CursorCommand` (alias of `i32`)
- `CursorDirection` (alias of `i32`)

### Color/style API
- `reset() -> string`
- `fg(color: Color) -> string`
- `bg(color: Color) -> string`
- `bold_on() -> string`
- `bold_off() -> string`
- `underline_on() -> string`
- `underline_off() -> string`
- `inverse_on() -> string`
- `inverse_off() -> string`

### Cursor API
- `cursor(cmd: CursorCommand) -> string`
- `cursor_move(dir: CursorDirection, amount: i32) -> string`

### Enum value helpers
- Color helpers: `color_default`, `color_black`, `color_red`, `color_green`,
  `color_yellow`, `color_blue`, `color_magenta`, `color_cyan`, `color_white`,
  `color_bright_black`, `color_bright_red`, `color_bright_green`,
  `color_bright_yellow`, `color_bright_blue`, `color_bright_magenta`,
  `color_bright_cyan`, `color_bright_white`
- Cursor command helpers:
  `cmd_home`, `cmd_clear_screen`, `cmd_clear_line`, `cmd_hide`, `cmd_show`,
  `cmd_save`, `cmd_restore`
- Cursor direction helpers: `dir_up`, `dir_down`, `dir_right`, `dir_left`

## std::fs

Module file: `stdlib/std/fs.mla`

### Types
- `File`
- `BufReader`

### File API
- `File::open(path: string) -> Result<File, string>`
- `File::create(path: string) -> Result<File, string>`
- `File::close(self: File) -> i32`
- `File::write(self: File, s: string) -> Result<i64, string>`
- `File::write_line(self: File, s: string) -> Result<i64, string>`

### Reader API
- `BufReader::new(file: File) -> BufReader`
- `BufReader::with_capacity(file: File, capacity: i64) -> BufReader`
- `BufReader::read_line(self: BufReader, buf: string) -> Result<i64, string>`
- `BufReader::lines(self: BufReader) -> list<string>`
- `free_lines(lines: list<string>) -> void`

## std::io

Module file: `stdlib/std/io.mla`

### Handles and trait-like wrappers
- `Stdin`
- `Stdout`
- `Stderr`
- `StdoutLock`
- `Cursor`
- `Read`
- `Write`
- `Seek`
- `BufRead`

### Core constructors/helpers
- `stdin() -> Stdin`
- `stdout() -> Stdout`
- `stderr() -> Stderr`
- `cursor_with_capacity(capacity: i64) -> Cursor`
- `cursor_from_string(s: string) -> Cursor`
- `cursor_free(c: Cursor) -> void`

### Stream read/write
- `read_line(input: Stdin, buf: string, capacity: i64) -> i64`
- `read_line_nonblocking(input: Stdin, buf: string, capacity: i64) -> i64`
- `write(out: Stdout, s: string) -> i64`
- `write(err: Stderr, s: string) -> i64`
- `writeln(out: Stdout, s: string) -> i64`
- `writeln(err: Stderr, s: string) -> i64`
- `flush(out: Stdout) -> int`
- `flush(err: Stderr) -> int`

### Buffering controls
- `buffering_unbuffered() -> int`
- `buffering_line() -> int`
- `buffering_full() -> int`
- `set_stdin_buffering(mode: int, size: i64) -> int`
- `set_stdout_buffering(mode: int, size: i64) -> int`
- `set_stderr_buffering(mode: int, size: i64) -> int`

### Locking and synchronized writes
- `lock(out: Stdout) -> StdoutLock`
- `unlock(lockToken: StdoutLock) -> int`
- `try_lock(out: Stdout) -> int`
- `write_locked(lockToken: StdoutLock, s: string) -> i64`
- `writeln_locked(lockToken: StdoutLock, s: string) -> i64`
- `write_sync(out: Stdout, s: string) -> i64`
- `writeln_sync(out: Stdout, s: string) -> i64`
- `__drop(lockToken: StdoutLock) -> void`

### Trait-like adapters and operations
- `as_read(input: Stdin) -> Read`
- `as_read(c: Cursor) -> Read`
- `as_write(out: Stdout) -> Write`
- `as_write(err: Stderr) -> Write`
- `as_write(c: Cursor) -> Write`
- `as_seek(c: Cursor) -> Seek`
- `as_buf_read(input: Stdin) -> BufRead`
- `as_buf_read(c: Cursor) -> BufRead`
- `read(reader: Read, buf: string, capacity: i64) -> i64`
- `write(writer: Write, s: string) -> i64`
- `read_line(reader: BufRead, buf: string, capacity: i64) -> i64`
- `seek(seeker: Seek, offset: i64, whence: int) -> i64`
- `to_string(c: Cursor) -> string`

## std::json

Module file: `stdlib/std/json.mla`

### Types
- `JsonDoc`
- `JsonValue`
- `JsonArrayIter`
- `JsonObjectIter`

### Kind constants
- `kind_invalid()`
- `kind_null()`
- `kind_bool()`
- `kind_number()`
- `kind_string()`
- `kind_array()`
- `kind_object()`

### Document API
- `JsonDoc::parse(text: string) -> Result<JsonDoc, string>`
- `JsonDoc::from_file(path: string) -> Result<JsonDoc, string>`
- `JsonDoc::root(self: JsonDoc) -> Result<JsonValue, string>`
- `JsonDoc::stringify(self: JsonDoc) -> Result<string, string>`
- `JsonDoc::stringify_pretty(self: JsonDoc) -> Result<string, string>`
- `JsonDoc::free(self: JsonDoc) -> void`
- `last_error() -> string`

### Value API
- `JsonValue::kind(self: JsonValue) -> int`
- `JsonValue::size(self: JsonValue) -> Result<i64, string>`
- `JsonValue::get(self: JsonValue, key: string) -> Result<JsonValue, string>`
- `JsonValue::index(self: JsonValue, i: i64) -> Result<JsonValue, string>`
- `JsonValue::as_bool(self: JsonValue) -> Result<int, string>`
- `JsonValue::as_i64(self: JsonValue) -> Result<i64, string>`
- `JsonValue::as_f64(self: JsonValue) -> Result<double, string>`
- `JsonValue::as_string(self: JsonValue) -> Result<string, string>`
- `JsonValue::key_at(self: JsonValue, i: i64) -> Result<string, string>`
- `JsonValue::iter_array(self: JsonValue) -> Result<JsonArrayIter, string>`
- `JsonValue::iter_object(self: JsonValue) -> Result<JsonObjectIter, string>`
- `JsonValue::free(self: JsonValue) -> void`

### Iterator API
- `JsonArrayIter::has_next(self: JsonArrayIter) -> int`
- `JsonArrayIter::current(self: JsonArrayIter) -> Result<JsonValue, string>`
- `JsonArrayIter::advance(self: JsonArrayIter) -> JsonArrayIter`
- `JsonObjectIter::has_next(self: JsonObjectIter) -> int`
- `JsonObjectIter::current_key(self: JsonObjectIter) -> Result<string, string>`
- `JsonObjectIter::current_value(self: JsonObjectIter) -> Result<JsonValue, string>`
- `JsonObjectIter::advance(self: JsonObjectIter) -> JsonObjectIter`

## std::jsonrpc

Module file: `stdlib/std/jsonrpc.mla`

### Types
- `StdioTransport`
- `Runtime`

### Transport API
- `stdio() -> StdioTransport`
- `StdioTransport::read_frame(self, buf: string, capacity: i64) -> Result<i64, string>`
- `StdioTransport::read_frame_timeout(self, buf: string, capacity: i64, timeout_ms: i64) -> Result<i64, string>`
- `StdioTransport::write_frame(self, payload: string) -> Result<i32, string>`
- `build_frame(payload: string) -> string`
- `parse_frame(frame: string, out: string, capacity: i64) -> Result<i64, string>`
- `last_error() -> string`

### Runtime queues
- `Runtime::new(queue_capacity: i64) -> Result<Runtime, string>`
- `Runtime::push_inbound(self, payload: string) -> Result<i32, string>`
- `Runtime::try_pop_inbound(self, buf: string, capacity: i64) -> Result<i64, string>`
- `Runtime::push_outbound(self, payload: string) -> Result<i32, string>`
- `Runtime::try_pop_outbound(self, buf: string, capacity: i64) -> Result<i64, string>`
- `Runtime::close(self) -> void`
- `flush_one_outbound(rt: Runtime, transport: StdioTransport, scratch: string, capacity: i64) -> Result<i32, string>`
- `run_stdio_loop(worker_count: i32, frame_capacity: i64, response_capacity: i64) -> Result<i32, string>`

### Cancellation API
- `cancel_mark(request_id: i64) -> Result<i32, string>`
- `cancel_is_marked(request_id: i64) -> int`
- `cancel_take(request_id: i64) -> int`
- `cancel_clear(request_id: i64) -> int`
- `cancel_clear_all() -> i32`
- `register_cancel_from_payload(payload: string) -> Result<i32, string>`
- `is_timeout_error(err: string) -> int`

### Runtime Dispatch Hook
- `__mlang_std_jsonrpc_runtime_dispatch(request_payload: string) -> string`
- Provided as a weak default in `libmlang_std` (returns empty response).
- Override this symbol in your server program to implement method dispatch.

## std::net

Module file: `stdlib/std/net.mla`

### Types
- `TcpListener`
- `TcpStream`

### Listener API
- `TcpListener::bind(addr: string, port: i64) -> Result<TcpListener, string>`
- `TcpListener::accept(self: TcpListener) -> Result<TcpStream, string>`
- `TcpListener::local_port(self: TcpListener) -> Result<i64, string>`
- `TcpListener::close(self: TcpListener) -> i32`
- `TcpListener::set_backlog(self: TcpListener, backlog: i64) -> Result<i32, string>`

### Stream API
- `TcpStream::connect(addr: string, port: i64) -> Result<TcpStream, string>`
- `TcpStream::read(self: TcpStream, buf: string, capacity: i64) -> Result<i64, string>`
- `TcpStream::write(self: TcpStream, s: string) -> Result<i64, string>`
- `TcpStream::close(self: TcpStream) -> i32`
- `TcpStream::set_nonblocking(self: TcpStream, enabled: int) -> Result<i32, string>`
- `TcpStream::set_read_timeout_ms(self: TcpStream, timeout_ms: i64) -> Result<i32, string>`
- `TcpStream::set_write_timeout_ms(self: TcpStream, timeout_ms: i64) -> Result<i32, string>`
- `TcpStream::try_clone(self: TcpStream) -> Result<TcpStream, string>`
- `TcpStream::from_handle(handle: i64) -> TcpStream`
- `TcpStream::raw_handle(self: TcpStream) -> i64`

### Errors
- `last_error() -> string`

## std::printf

Module file: `stdlib/std/printf.mla`

MLang-facing wrappers over C-style output that accept preformatted strings.

### API
- `printf(s: string) -> void`
- `eprintf(s: string) -> void`
- `fprintf(fd: i32, s: string) -> void`

## std::math

Module file: `stdlib/std/math.mla`

### Numeric helpers (overloaded for `int`, `float`, `double`)
- `add(a, b)`
- `subtract(a, b)`
- `multiply(a, b)`
- `square(x)`
- `abs(x)`
- `min(a, b)`
- `max(a, b)`
- `clamp(x, low, high)`
- `pow(a, b)`
- `sqrt(x)`
- `sin(x)`
- `cos(x)`
- `tan(x)`
- `floor(x)`
- `ceil(x)`
- `round(x)`
- `log(x)`
- `exp(x)`
- `modulo(a, b)`

### Integer-specific
- `sum_range(start: int, end: int) -> int`
- `factorial(n: int) -> int`

## std::process

Module file: `stdlib/std/process.mla`

### Types
- `Child`
- `ChildStdin`
- `ChildStdout`
- `ChildStderr`
- `ExitStatus`

### Spawn
- `spawn(program: string, args: list<string>) -> Result<Child, string>`
- `spawn_inherit(program: string, args: list<string>) -> Result<Child, string>`
- `last_error() -> string`

### Child and pipe API
- `Child::stdin(self: Child) -> Result<ChildStdin, string>`
- `Child::stdout(self: Child) -> Result<ChildStdout, string>`
- `Child::stderr(self: Child) -> Result<ChildStderr, string>`
- `Child::wait(self: Child) -> Result<ExitStatus, string>`
- `Child::kill(self: Child, sig: i32) -> Result<i32, string>`
- `Child::close(self: Child) -> i32`
- `ChildStdin::write(self: ChildStdin, s: string) -> Result<i64, string>`
- `ChildStdin::close(self: ChildStdin) -> i32`
- `ChildStdout::read(self: ChildStdout, buf: string, capacity: i64) -> Result<i64, string>`
- `ChildStdout::close(self: ChildStdout) -> i32`
- `ChildStderr::read(self: ChildStderr, buf: string, capacity: i64) -> Result<i64, string>`
- `ChildStderr::close(self: ChildStderr) -> i32`

### Exit status
- `ExitStatus::success(self: ExitStatus) -> int`
- `ExitStatus::exited(self: ExitStatus) -> int`
- `ExitStatus::code(self: ExitStatus) -> i32`
- `ExitStatus::signaled(self: ExitStatus) -> int`
- `ExitStatus::signal(self: ExitStatus) -> i32`

## std::regex

Module file: `stdlib/std/regex.mla`

### Types
- `Regex`

### Compile / lifetime
- `Regex::compile(pattern: string) -> Result<Regex, string>`
- `Regex::close(self: Regex) -> i32`

### Matching
- `Regex::is_match(self: Regex, text: string) -> i32`
- `Regex::find_start(self: Regex, text: string) -> i64`
- `Regex::find_end(self: Regex, text: string) -> i64`
- `Regex::match_start(self: Regex, text: string, group_index: i64) -> i64`
- `Regex::match_end(self: Regex, text: string, group_index: i64) -> i64`

### Errors
- `last_error() -> string`

## std::sync

Module file: `stdlib/std/sync.mla`

### Types
- `Mutex`
- `Condvar`
- `Channel`
- `LockFreeQueue` (SPSC string queue)

### Mutex
- `Mutex::new() -> Result<Mutex, string>`
- `Mutex::lock(self: Mutex) -> Result<i32, string>`
- `Mutex::unlock(self: Mutex) -> Result<i32, string>`
- `Mutex::close(self: Mutex) -> i32`

### Condvar
- `Condvar::new() -> Result<Condvar, string>`
- `Condvar::wait(self: Condvar, mutex: Mutex) -> Result<i32, string>`
- `Condvar::wait_timeout_ms(self: Condvar, mutex: Mutex, timeout_ms: i64) -> Result<i32, string>`
- `Condvar::notify_one(self: Condvar) -> Result<i32, string>`
- `Condvar::notify_all(self: Condvar) -> Result<i32, string>`
- `Condvar::close(self: Condvar) -> i32`

### Channel (string)
- `Channel::new(capacity: i64) -> Result<Channel, string>`
- `Channel::send(self: Channel, s: string) -> Result<i32, string>`
- `Channel::post(self: Channel, s: string) -> Result<i32, string>` (alias of `send`)
- `Channel::recv(self: Channel, buf: string, capacity: i64) -> Result<i64, string>`
- `Channel::try_recv(self: Channel, buf: string, capacity: i64) -> Result<i64, string>`
- `Channel::close(self: Channel) -> i32`
- `Channel::free(self: Channel) -> i32`

### LockFreeQueue (string, single-producer/single-consumer)
- `LockFreeQueue::new(capacity: i64) -> Result<LockFreeQueue, string>`
- `LockFreeQueue::try_send(self: LockFreeQueue, s: string) -> Result<i32, string>`
  - returns `0` on success, `1` when full
- `LockFreeQueue::try_recv(self: LockFreeQueue, buf: string, capacity: i64) -> Result<i64, string>`
  - returns bytes copied (>0), `-2` when empty, `0` when closed and drained
- `LockFreeQueue::close(self: LockFreeQueue) -> i32`
- `LockFreeQueue::free(self: LockFreeQueue) -> i32`

## std::term

Module file: `stdlib/std/term.mla`

Terminal capability detection and termios helpers.

### Types
- `TermSize`
- `TerminalCaps`
- `ColorLevel` (`i32` alias)

### Color level constants
- `color_none() -> ColorLevel` (0)
- `color_16() -> ColorLevel` (16)
- `color_256() -> ColorLevel` (256)
- `color_truecolor() -> ColorLevel` (16777216)

### Capability queries
- `term_name() -> string`
- `supports_ansi() -> i32`
- `stdin_is_tty() -> i32`
- `stdout_is_tty() -> i32`
- `stderr_is_tty() -> i32`
- `stdout_size() -> TermSize`
- `stderr_size() -> TermSize`
- `stdout_color_level() -> ColorLevel`
- `stderr_color_level() -> ColorLevel`
- `stdout_truecolor() -> i32`
- `stderr_truecolor() -> i32`
- `stdout_caps() -> TerminalCaps`
- `stderr_caps() -> TerminalCaps`

### termios helpers
- `stdin_enable_raw() -> i32`
- `stdin_restore() -> i32`

## std::time

Module file: `stdlib/std/time.mla`

### Clock
- `now_ms() -> i64`
- `now_ns() -> i64`
- `sleep_ms(ms: i64) -> void`

### Timer
- `Timer::after(timeout_ms: i64) -> Result<Timer, string>`
- `Timer::reset(self: Timer, timeout_ms: i64) -> i32`
- `Timer::elapsed(self: Timer) -> int`
- `Timer::remaining_ms(self: Timer) -> i64`
- `Timer::wait(self: Timer) -> i32`
- `Timer::close(self: Timer) -> i32`

## std::timer

Module file: `stdlib/std/timer.mla`

### Types
- `IntervalTimer`
- `AsyncTicker`

### Interval timer API
- `IntervalTimer::every_ms(interval_ms: i64) -> Result<IntervalTimer, string>`
- `IntervalTimer::reset(self: IntervalTimer) -> i32`
- `IntervalTimer::remaining_ms(self: IntervalTimer) -> i64`
- `IntervalTimer::wait_next(self: IntervalTimer) -> i32`
- `IntervalTimer::poll(self: IntervalTimer) -> i32`
- `IntervalTimer::close(self: IntervalTimer) -> i32`

### Async ticker API
- `AsyncTicker::start(queue_handle: i64, interval_ms: i64, event_name: string) -> Result<AsyncTicker, string>`
- `AsyncTicker::stop(self: AsyncTicker) -> i32`
- `AsyncTicker::close(self: AsyncTicker) -> i32`

## std::strbuf

Module file: `stdlib/std/strbuf.mla`

### Allocation
- `String::new() -> string` (compiler intrinsic wrapper)
- `String::with_capacity(capacity: i64) -> string` (compiler intrinsic wrapper)
- `String::free(buf: string) -> void` (compiler intrinsic wrapper)
- `new() -> string`
- `with_capacity(capacity: i64) -> string`
- `free(buf: string) -> void`

### Dynamic builder
- `StringBuilder`
- `builder_new(initial_capacity: i64) -> StringBuilder` (default page: 256 bytes)
- `builder_with_page(initial_capacity: i64, page_size: i64) -> StringBuilder`
- `builder_is_valid(builder: StringBuilder) -> i32`
- `builder_len(builder: StringBuilder) -> i64`
- `builder_capacity(builder: StringBuilder) -> i64`
- `builder_set_page_size(builder: StringBuilder, page_size: i64) -> i32`
- `builder_clear(builder: StringBuilder) -> i32`
- `builder_reserve(builder: StringBuilder, min_capacity: i64) -> i32`
- `builder_append(builder: StringBuilder, s: string) -> i64` (bytes appended, `-1` on failure)
- `builder_append_char(builder: StringBuilder, ch: i32) -> i64` (`1` on success, `-1` on failure)
- `builder_to_string(builder: StringBuilder) -> string` (clone)
- `builder_take_string(builder: StringBuilder) -> string` (moves out current buffer and resets builder)
- `builder_free(builder: StringBuilder) -> void`

### String helpers
- `len(s: string) -> i64`
- `is_empty(s: string) -> int`
- `clone(s: string) -> string`
- `eq(a: string, b: string) -> int`
- `compare(a: string, b: string) -> i64`
- `starts_with(s: string, prefix: string) -> int`
- `ends_with(s: string, suffix: string) -> int`
- `contains(s: string, needle: string) -> int`
- `find(s: string, needle: string) -> i64`
- `rfind(s: string, needle: string) -> i64`
- `concat(a: string, b: string) -> string`
- `repeat(s: string, count: i64) -> string`
- `trim(s: string) -> string`
- `ltrim(s: string) -> string`
- `rtrim(s: string) -> string`

### Unicode conversion
- `to_utf16(s: string) -> str16`
- `from_utf16(s: str16) -> string`
- `to_utf8(s: str16) -> string`
- `free_utf16(s: str16) -> void`

## std::bytes

Module file: `stdlib/std/bytes.mla`

### Types
- `Bytes`

### Lifecycle
- `Bytes::new(initial_capacity: i64) -> Result<Bytes, string>`
- `Bytes::from_string(s: string) -> Result<Bytes, string>`
- `Bytes::close(self: Bytes) -> i32`
- `last_error() -> string`

### Buffer operations
- `Bytes::len(self: Bytes) -> i64`
- `Bytes::capacity(self: Bytes) -> i64`
- `Bytes::clear(self: Bytes) -> i32`
- `Bytes::reserve(self: Bytes, min_capacity: i64) -> i32`
- `Bytes::append_byte(self: Bytes, value: i32) -> i32`
- `Bytes::append_string(self: Bytes, s: string) -> i64`
- `Bytes::append_bytes(self: Bytes, other: Bytes) -> i64`
- `Bytes::get(self: Bytes, index: i64) -> i32`
- `Bytes::set(self: Bytes, index: i64, value: i32) -> i32`

### Conversions
- `Bytes::to_string(self: Bytes) -> string` (text-oriented C/UTF-8 string copy)
- `Bytes::to_hex(self: Bytes) -> string` (binary-safe lowercase hex)

## std::thread

Module file: `stdlib/std/thread.mla`

### Thread and mutex
- `join(handle: Handle<Thread>) -> int`
- `mutex_new() -> Handle<Mutex>`
- `mutex_lock_handle(handle: Handle<Mutex>) -> int`
- `mutex_unlock_handle(handle: Handle<Mutex>) -> int`
- `mutex_free(handle: Handle<Mutex>) -> int`

### Atomics
- `atomic_new(initial: i64) -> Handle<Atomic64>`
- `atomic_load_value(handle: Handle<Atomic64>) -> i64`
- `atomic_store_value(handle: Handle<Atomic64>, value: i64) -> i64`
- `atomic_add_value(handle: Handle<Atomic64>, delta: i64) -> i64`
- `atomic_free_handle(handle: Handle<Atomic64>) -> void`

## std::vec

Module file: `stdlib/std/vec.mla`

`Vec<T>` is a type alias for `list<T>`. The two are interchangeable in all
contexts. Methods listed below are compiler intrinsics backed by `libmlang_std`
and are also summarized in "Built-in Collection Methods (Compiler Intrinsics)"
above.

### Constructors

- `Vec::new() -> Vec<T>` — create an empty Vec; element type inferred from context
- `Vec::new_i32() -> Vec<i32>` — empty Vec with explicit `i32` element type
- `Vec::new_i64() -> Vec<i64>` — empty Vec with explicit `i64` element type
- `Vec::new_str() -> Vec<string>` — empty Vec with explicit `string` element type

### Macros

- `vec![a, b, c]` — construct a Vec from a comma-separated list of elements
- `vec![val; N]` — construct a Vec of `N` copies of `val`

### Size

- `v.len() -> i64` — number of elements currently stored
- `v.is_empty() -> bool` — `true` when the Vec contains no elements

### Mutation

- `v.push(val)` — append `val` to the end (grows the Vec by one)
- `v.pop() -> T` — remove and return the last element; panics if empty
- `v.clear()` — remove all elements (Vec remains valid for further pushes)

### Search

- `v.contains(val) -> bool` — `true` if `val` is present in the Vec
- `v.index_of(val) -> i64` — zero-based index of first match, or `-1` if absent

### Ordering

- `v.sort()` — sort elements in ascending order (in-place)
- `v.sort_desc()` — sort elements in descending order (in-place)
- `v.reverse()` — reverse element order (in-place)
- `v.dedup()` — remove consecutive duplicate elements; sort first to deduplicate all

### Access

- `v.first() -> T` — return the first element; panics if empty
- `v.last() -> T` — return the last element; panics if empty

### Iteration

```mlang
// for-in loop
for x in v {
    println!("{}", x);
}

// enumerated loop
for(i, x) in v.enumerate() {
    println!("v[{}] = {}", i, x);
}
```
