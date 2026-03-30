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
mod std::chat;
mod std::exceptions;
mod std::math;
mod std::algorithm::order;
mod std::algorithm::numeric;
mod std::algorithm::ranges;
mod std::algorithm::fft;
mod std::net;
mod std::printf;
mod std::process;
mod std::signal;
mod std::rand;
mod std::regex;
mod std::sed;
mod std::sync;
mod std::term;
mod std::time;
mod std::timer;
mod std::fs;
mod std::strbuf;
mod std::bytes;
mod std::serde;
mod std::protocol;
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
- `stdlib/std/chat.mla`
- `stdlib/std/exceptions.mla`
- `stdlib/std/math.mla`
- `stdlib/std/algorithm/order.mla`
- `stdlib/std/algorithm/numeric.mla`
- `stdlib/std/algorithm/ranges.mla`
- `stdlib/std/algorithm/fft.mla`
- `stdlib/std/net.mla`
- `stdlib/std/printf.mla`
- `stdlib/std/process.mla`
- `stdlib/std/signal.mla`
- `stdlib/std/rand.mla`
- `stdlib/std/regex.mla`
- `stdlib/std/sed.mla`
- `stdlib/std/sync.mla`
- `stdlib/std/term.mla`
- `stdlib/std/time.mla`
- `stdlib/std/timer.mla`
- `stdlib/std/strbuf.mla`
- `stdlib/std/bytes.mla`
- `stdlib/std/serde.mla`
- `stdlib/std/protocol.mla`
- `stdlib/std/testing.mla`
- `stdlib/std/thread.mla`
- `stdlib/std/unordered.mla`
- `stdlib/std/vec.mla`

## Built-in Collection Methods (Compiler Intrinsics)

These methods are language/compiler intrinsics and are available directly on
collection/str8 values (including values typed through `use type` aliases).

## Built-in Enum Behavior

Enum declarations define named integral values and may specify an explicit
backing type such as `u8` or `i64`.

- Enum values format/print as `EnumName::Variant`.
- `for value in Status { ... }` iterates the enum in declaration order.
- `for (i, value) in Status.enumerate() { ... }` iterates in declaration order
  and exposes a zero-based `i64` index.

Example:

```mla
enum Status : u8 {
    Invalid = 1,
    Busy = 2,
    Success = 3
};

for value in Status {
    println!("{}", value);
}

for (i, value) in Status.enumerate() {
    println!("[{}] {}", i, value);
}
```

### `str8`
`String` in `String::new/with_capacity/free` is a compiler wrapper namespace
for this builtin type, not a distinct type declaration.

- `s.len() -> i64`
- `s.is_empty() -> i32`

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
use type SomeMap = map<str8, i32>;

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
- `ArgParser::new(prog: str8, desc: str8) -> ArgParser`
- `ArgParser::flag(self: ArgParser, long_name: str8, short_name: str8, help: str8) -> void`
- `ArgParser::option(self: ArgParser, long_name: str8, short_name: str8, help: str8, default_val: str8) -> void`
- `ArgParser::positional(self: ArgParser, name: str8, help: str8) -> void`
- `ArgParser::parse(self: ArgParser, argc: i32, args: list<str8>) -> ParseResult`
- `ArgParser::print_help(self: ArgParser) -> void`
- `ArgParser::free(self: ArgParser) -> void`

### Parse results
- `ParseResult::ok(self: ParseResult) -> bool`
- `ParseResult::has_error(self: ParseResult) -> bool`
- `ParseResult::error(self: ParseResult) -> str8`
- `ParseResult::help_requested(self: ParseResult) -> bool`
- `ParseResult::flag(self: ParseResult, name: str8) -> bool`
- `ParseResult::get(self: ParseResult, name: str8) -> str8`
- `ParseResult::get_i64(self: ParseResult, name: str8) -> i64`
- `ParseResult::positional(self: ParseResult, idx: i64) -> str8`
- `ParseResult::positional_count(self: ParseResult) -> i64`
- `ParseResult::free(self: ParseResult) -> void`

## std::chat

Module file: `stdlib/std/chat.mla`

### Types
- `ChatUi`

### API
- `line_normal() -> i32`
- `line_system() -> i32`
- `line_self() -> i32`
- `last_error() -> str8`
- `ChatUi::new(max_lines: i64) -> Result<ChatUi, str8>`
- `ChatUi::close(self: ChatUi) -> i32`
- `ChatUi::set_title(self: ChatUi, title: str8) -> i32`
- `ChatUi::set_server(self: ChatUi, server: str8) -> i32`
- `ChatUi::set_channel(self: ChatUi, channel: str8) -> i32`
- `ChatUi::set_nick(self: ChatUi, nick: str8) -> i32`
- `ChatUi::set_status(self: ChatUi, status: str8) -> i32`
- `ChatUi::set_prompt(self: ChatUi, prompt: str8) -> i32`
- `ChatUi::set_input(self: ChatUi, input: str8) -> i32`
- `ChatUi::input(self: ChatUi) -> str8`
- `ChatUi::push_line(self: ChatUi, prefix: str8, text: str8) -> i32`
- `ChatUi::push_system(self: ChatUi, text: str8) -> i32`
- `ChatUi::push_self(self: ChatUi, prefix: str8, text: str8) -> i32`
- `ChatUi::feed_keycode(self: ChatUi, keycode: i32) -> i32`
- `ChatUi::scroll(self: ChatUi, delta: i64) -> i32`
- `ChatUi::take_submitted(self: ChatUi) -> str8`
- `ChatUi::render(self: ChatUi, rows: i32, cols: i32) -> str8`

## std::exceptions

Module file: `stdlib/std/exceptions.mla`

This module provides the runtime payload type used by the language-level
`throw` and `try/catch` syntax.

### Types
- `Exception`

### Language usage

Basic throw/catch:

```mla
mod std::exceptions;
use std::exceptions::*;

fn parse_number(text: str8) -> i32 {
    if text == "42" {
        return 42;
    }
    throw with_line("ParseError", "expected 42", 12);
}

fn main() -> i32 {
    try {
        let value: i32 = parse_number("x");
        println!("{}", value);
    } catch e: Exception {
        println!("caught {} at {}: {}", e.type_name, e.source_line, e.message);
    }
    return 0;
}
```

Notes:
- `throw expr;` transfers control to the nearest enclosing `catch`.
- `catch e: Exception` binds the thrown payload for the handler block.
- If no handler is active, the runtime prints the uncaught exception and aborts.
- Scope-owned values are cleaned up during unwind before control reaches `catch`.

### API
- `Exception`
  Fields:
  - `type_name: str8`
  - `message: str8`
  - `source_line: i32`
  - `owned: bool`
- `new(type_name: str8, message: str8) -> Exception`
- `with_line(type_name: str8, message: str8, source_line: i32) -> Exception`
- `free(ex: Exception) -> void`

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
- `session_create() -> Result<Session, str8>`
- `last_status() -> i32`
- `status_name(status: i32) -> str8`
- `version() -> str8`
- `last_error() -> str8`

### Session lifecycle and document state
- `Session::destroy(self: Session) -> Result<i32, str8>`
- `Session::open_document(self: Session, uri: str8, language_id: str8, text: str8, version: i32) -> Result<i32, str8>`
- `Session::change_document(self: Session, uri: str8, text: str8, version: i32) -> Result<i32, str8>`
- `Session::close_document(self: Session, uri: str8) -> Result<i32, str8>`

### Diagnostics and editor queries
- `Session::syntax_diagnostic_count(self: Session, uri: str8) -> Result<i32, str8>`
- `Session::syntax_diagnostic_get(self: Session, uri: str8, index: i32) -> Result<SyntaxDiagnostic, str8>`
- `Session::hover(self: Session, uri: str8, line: i32, column: i32) -> Result<str8, str8>`
- `Session::completion_count(self: Session, uri: str8, line: i32, column: i32) -> Result<i32, str8>`
- `Session::completion_get(self: Session, uri: str8, line: i32, column: i32, index: i32) -> Result<str8, str8>`

### Symbols, definitions, references, rename
- `Session::document_symbol_count(self: Session, uri: str8) -> Result<i32, str8>`
- `Session::document_symbol_get(self: Session, uri: str8, index: i32) -> Result<DocumentSymbol, str8>`
- `Session::definition(self: Session, uri: str8, line: i32, column: i32) -> Result<DefinitionLocation, str8>`
- `Session::references_count(self: Session, uri: str8, line: i32, column: i32) -> Result<i32, str8>`
- `Session::reference_get(self: Session, uri: str8, line: i32, column: i32, index: i32) -> Result<ReferenceLocation, str8>`
- `Session::resolve_symbol(self: Session, uri: str8, line: i32, column: i32) -> Result<ResolvedSymbol, str8>`
- `Session::rename_is_safe(self: Session, uri: str8, line: i32, column: i32, new_name: str8) -> Result<i32, str8>`
- `Session::semantic_cache_warm(self: Session, uri: str8) -> Result<i32, str8>`
- `Session::semantic_cache_clear(self: Session) -> Result<i32, str8>`

## std::date

Module file: `stdlib/std/date.mla`

### Types
- `DateTime`

### API
- `now() -> DateTime`
- `format_iso8601(dt: DateTime) -> str8`
- `format_date(dt: DateTime) -> str8`
- `format_time(dt: DateTime) -> str8`

## std::env

Module file: `stdlib/std/env.mla`

### API
- `args() -> list<str8>`
- `len(values: &list<str8>) -> i64`
- `get(values: &list<str8>, index: i64) -> str8`
- `cwd() -> str8`
- `get(name: str8) -> str8`
- `println(msg: str8) -> void`
- `eprintln(msg: str8) -> void`
- `wants_help(values: list<str8>) -> i32`
- `arg_or(values: list<str8>, index: i64, fallback: str8) -> str8`
- `render_help(app: str8, overview: str8, options: list<HelpOption>, total_width: i64) -> str8`

## std::fs

Module file: `stdlib/std/fs.mla`

### Directory helpers
- `file_exists(path: str8) -> i32`
- `is_dir(path: str8) -> i32`
- `parent_dir(path: str8) -> str8`
- `cwd() -> str8`
- `chdir(path: str8) -> i32`
- `mkdir_p(path: str8) -> i32`
- `remove_tree(path: str8) -> i32`
- `list_dir(path: str8) -> list<str8>`
- `glob_recursive(root: str8, pattern: str8) -> list<str8>`

## std::event_loop

Module file: `stdlib/std/event_loop.mla`

### Types
- `EventLoop`

### API
- `EventLoop::start(queue_handle: i64, interval_ms: i64, event_name: str8) -> Result<EventLoop, str8>`
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
- `expect_eq(expected, actual)` overloads for `i64`, `bool`, `str8`, `f32`, `f64`
- `expect_eq_i32(expected: i32, actual: i32)`
- `expect_eq_i64(expected: i64, actual: i64)`
- `expect_not_eq(left, right)` overloads for `i64`, `bool`, `str8`, `f32`, `f64`
- `expect_not_eq_i32(left: i32, right: i32)`
- `expect_not_eq_i64(left: i64, right: i64)`
- `expect_array_eq(expected, actual)` overloads for `list<i64>`, `list<i32>`, `list<bool>`, `list<str8>`
- `expect_array_not_eq(left, right)` overloads for `list<i64>`, `list<i32>`, `list<bool>`, `list<str8>`

Fatal verify helpers (abort on failure):
- `verify_true(cond: bool)`
- `verify(cond: bool)` (alias of `verify_true`)
- `verify_false(cond: bool)`
- `verify_eq(expected, actual)` overloads for `i64`, `bool`, `str8`, `f32`, `f64`
- `verify_eq_i32(expected: i32, actual: i32)`
- `verify_eq_i64(expected: i64, actual: i64)`
- `verify_not_eq(left, right)` overloads for `i64`, `bool`, `str8`, `f32`, `f64`
- `verify_not_eq_i32(left: i32, right: i32)`
- `verify_not_eq_i64(left: i64, right: i64)`
- `verify_array_eq(expected, actual)` overloads for `list<i64>`, `list<i32>`, `list<bool>`, `list<str8>`
- `verify_array_not_eq(left, right)` overloads for `list<i64>`, `list<i32>`, `list<bool>`, `list<str8>`

Failure diagnostics for `expect_eq`/`verify_eq` and `expect_not_eq`/`verify_not_eq`
include compared values (`expected/actual` or `left/right`) in log output.

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
fn test_demo() {
    reset();
    expect_true(2 > 1);
    verify_eq("ok", "ok");
    verify_not_eq("left", "right");
}
```

## std::esc

Module file: `stdlib/std/esc.mla`

ANSI escape helpers for terminal color/style and cursor control. API is
value-set-driven so call sites avoid raw numeric SGR/control codes.
Sequences auto-disable to `""` when `std::term::supports_ansi()` is false.

Ownership note:
- most `std::esc` helpers return borrowed/static escape strings
- treat those returned `str8` values as read-only and do not call `String::free()` on them
- ACS box-drawing helpers (`acs_hline`, `acs_vline`, corners, tees, plus) are the exception here: they return owned formatted strings

### Value-set Types
- `Color` (alias of `i32`)
- `CursorCommand` (alias of `i32`)
- `CursorDirection` (alias of `i32`)

### Color/style API
- `reset() -> str8`
- `fg(color: Color) -> str8`
- `bg(color: Color) -> str8`
- `bold_on() -> str8`
- `bold_off() -> str8`
- `underline_on() -> str8`
- `underline_off() -> str8`
- `inverse_on() -> str8`
- `inverse_off() -> str8`
- `standout_on() -> str8`
- `standout_off() -> str8`
- `clear_eol() -> str8`
- `alt_screen_on() -> str8`
- `alt_screen_off() -> str8`

### Cursor API
- `cursor(cmd: CursorCommand) -> str8`
- `cursor_move(dir: CursorDirection, amount: i32) -> str8`

### ACS (ncurses-style line drawing)
- `acs_hline() -> str8`
- `acs_vline() -> str8`
- `acs_ulcorner() -> str8`
- `acs_urcorner() -> str8`
- `acs_llcorner() -> str8`
- `acs_lrcorner() -> str8`
- `acs_ltee() -> str8`
- `acs_rtee() -> str8`
- `acs_ttee() -> str8`
- `acs_btee() -> str8`
- `acs_plus() -> str8`

### TUI Safety Helper (recommended for all terminal UIs)
Use this pattern in every TUI example to avoid broken scrolling/cursor state:

```mla
mod std::esc;
mod std::term;
use std::esc::alt_screen_off;
use std::esc::alt_screen_on;
use std::esc::cmd_clear_screen;
use std::esc::cmd_hide;
use std::esc::cmd_home;
use std::esc::cmd_show;
use std::esc::cursor;
use std::esc::reset;
use std::term::stdin_enable_raw;
use std::term::stdin_is_tty;
use std::term::stdin_restore;

pub struct TuiGuard {
    var raw_enabled: i32;
};

pub fn tui_enter() -> TuiGuard {
    var raw: i32 = 0;
    if stdin_is_tty() == 1 {
        if stdin_enable_raw() == 0 {
            raw = 1;
        }
    }
    print!("{}{}{}{}", alt_screen_on(), cursor(cmd_hide()), cursor(cmd_home()), cursor(cmd_clear_screen()));
    return TuiGuard { raw_enabled: raw };
}

pub fn tui_leave(g: TuiGuard) -> void {
    if g.raw_enabled == 1 {
        let _ = stdin_restore();
    }
    print!("{}{}{}{}\n", reset(), cursor(cmd_show()), cursor(cmd_home()), alt_screen_off());
}
```

Shell runner safety (recommended):

```sh
cleanup_terminal() {
  stty sane 2>/dev/null || true
  printf '\033[0m\033[?25h\033[?1049l' || true
}
trap cleanup_terminal EXIT INT TERM
```

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
- `File::open(path: str8) -> Result<File, str8>`
- `File::create(path: str8) -> Result<File, str8>`
- `File::close(self: File) -> i32`
- `File::write(self: File, s: str8) -> Result<i64, str8>`
- `File::write_line(self: File, s: str8) -> Result<i64, str8>`

## Builtin `bit` and `sizeof`

Builtin reference source: `stdlib/types.mla`

### `bit`
- `bit` is a builtin logical 0/1 type
- Use `bit(expr)` to convert an integer or bool expression
- `sizeof(bit)` reports the ABI byte size

### `sizeof`
- `sizeof(Type) -> i64`
- `sizeof(expr) -> i64`
- Returns the ABI byte size in bytes

Examples:

```mla
var enabled: bit = 1;
println!("bit={} bool={} list_header={}",
         sizeof(bit), sizeof(bool), sizeof(list<bool>));
```

### `list<bool>` vs `std::bitset::BitSet`
- `list<bool>` is a normal list container, not a packed `std::vector<bool>`-style specialization
- Use `std::bitset::BitSet` when you need one-bit-per-entry dense storage
- `BitSet::len()` is measured in bits

## std::bits

Module file: `stdlib/std/bits.mla`

### Helpers
- `ON() -> bit`
- `OFF() -> bit`

These helpers provide readable aliases for the two builtin `bit` values.

### Reader API
- `BufReader::new(file: File) -> BufReader`
- `BufReader::with_capacity(file: File, capacity: i64) -> BufReader`
- `BufReader::read_line(self: BufReader, buf: str8) -> Result<i64, str8>`
- `BufReader::lines(self: BufReader) -> list<str8>`
- `free_lines(lines: list<str8>) -> void`

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
- `cursor_from_string(s: str8) -> Cursor`
- `cursor_free(c: Cursor) -> void`

### Stream read/write
- `read_line(input: Stdin, buf: str8, capacity: i64) -> i64`
- `read_line_nonblocking(input: Stdin, buf: str8, capacity: i64) -> i64`
- `write(out: Stdout, s: str8) -> i64`
- `write(err: Stderr, s: str8) -> i64`
- `writeln(out: Stdout, s: str8) -> i64`
- `writeln(err: Stderr, s: str8) -> i64`
- `flush(out: Stdout) -> i32`
- `flush(err: Stderr) -> i32`

### Buffering controls
- `buffering_unbuffered() -> i32`
- `buffering_line() -> i32`
- `buffering_full() -> i32`
- `set_stdin_buffering(mode: i32, size: i64) -> i32`
- `set_stdout_buffering(mode: i32, size: i64) -> i32`
- `set_stderr_buffering(mode: i32, size: i64) -> i32`

### Locking and synchronized writes
- `lock(out: Stdout) -> StdoutLock`
- `unlock(lockToken: StdoutLock) -> i32`
- `try_lock(out: Stdout) -> i32`
- `write_locked(lockToken: StdoutLock, s: str8) -> i64`
- `writeln_locked(lockToken: StdoutLock, s: str8) -> i64`
- `write_sync(out: Stdout, s: str8) -> i64`
- `writeln_sync(out: Stdout, s: str8) -> i64`
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
- `read(reader: Read, buf: str8, capacity: i64) -> i64`
- `write(writer: Write, s: str8) -> i64`
- `read_line(reader: BufRead, buf: str8, capacity: i64) -> i64`
- `seek(seeker: Seek, offset: i64, whence: i32) -> i64`
- `to_string(c: Cursor) -> str8`

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
- `JsonDoc::parse(text: str8) -> Result<JsonDoc, str8>`
- `JsonDoc::from_file(path: str8) -> Result<JsonDoc, str8>`
- `JsonDoc::root(self: JsonDoc) -> Result<JsonValue, str8>`
- `JsonDoc::stringify(self: JsonDoc) -> Result<str8, str8>`
- `JsonDoc::stringify_pretty(self: JsonDoc) -> Result<str8, str8>`
- `JsonDoc::free(self: JsonDoc) -> void`
- `last_error() -> str8`

### Value API
- `JsonValue::kind(self: JsonValue) -> i32`
- `JsonValue::size(self: JsonValue) -> Result<i64, str8>`
- `JsonValue::get(self: JsonValue, key: str8) -> Result<JsonValue, str8>`
- `JsonValue::index(self: JsonValue, i: i64) -> Result<JsonValue, str8>`
- `JsonValue::as_bool(self: JsonValue) -> Result<i32, str8>`
- `JsonValue::as_i64(self: JsonValue) -> Result<i64, str8>`
- `JsonValue::as_f64(self: JsonValue) -> Result<f64, str8>`
- `JsonValue::as_string(self: JsonValue) -> Result<str8, str8>`
- `JsonValue::key_at(self: JsonValue, i: i64) -> Result<str8, str8>`
- `JsonValue::iter_array(self: JsonValue) -> Result<JsonArrayIter, str8>`
- `JsonValue::iter_object(self: JsonValue) -> Result<JsonObjectIter, str8>`
- `JsonValue::free(self: JsonValue) -> void`

### Iterator API
- `JsonArrayIter::has_next(self: JsonArrayIter) -> i32`
- `JsonArrayIter::current(self: JsonArrayIter) -> Result<JsonValue, str8>`
- `JsonArrayIter::advance(self: JsonArrayIter) -> JsonArrayIter`
- `JsonObjectIter::has_next(self: JsonObjectIter) -> i32`
- `JsonObjectIter::current_key(self: JsonObjectIter) -> Result<str8, str8>`
- `JsonObjectIter::current_value(self: JsonObjectIter) -> Result<JsonValue, str8>`
- `JsonObjectIter::advance(self: JsonObjectIter) -> JsonObjectIter`

## std::jsonrpc

Module file: `stdlib/std/jsonrpc.mla`

### Types
- `StdioTransport`
- `Runtime`

### Transport API
- `stdio() -> StdioTransport`
- `StdioTransport::read_frame(self, buf: str8, capacity: i64) -> Result<i64, str8>`
- `StdioTransport::read_frame_timeout(self, buf: str8, capacity: i64, timeout_ms: i64) -> Result<i64, str8>`
- `StdioTransport::write_frame(self, payload: str8) -> Result<i32, str8>`
- `build_frame(payload: str8) -> str8`
- `parse_frame(frame: str8, out: str8, capacity: i64) -> Result<i64, str8>`
- `last_error() -> str8`

### Runtime queues
- `Runtime::new(queue_capacity: i64) -> Result<Runtime, str8>`
- `Runtime::push_inbound(self, payload: str8) -> Result<i32, str8>`
- `Runtime::try_pop_inbound(self, buf: str8, capacity: i64) -> Result<i64, str8>`
- `Runtime::push_outbound(self, payload: str8) -> Result<i32, str8>`
- `Runtime::try_pop_outbound(self, buf: str8, capacity: i64) -> Result<i64, str8>`
- `Runtime::close(self) -> void`
- `flush_one_outbound(rt: Runtime, transport: StdioTransport, scratch: str8, capacity: i64) -> Result<i32, str8>`
- `run_stdio_loop(worker_count: i32, frame_capacity: i64, response_capacity: i64) -> Result<i32, str8>`

### Cancellation API
- `cancel_mark(request_id: i64) -> Result<i32, str8>`
- `cancel_is_marked(request_id: i64) -> i32`
- `cancel_take(request_id: i64) -> i32`
- `cancel_clear(request_id: i64) -> i32`
- `cancel_clear_all() -> i32`
- `register_cancel_from_payload(payload: str8) -> Result<i32, str8>`
- `is_timeout_error(err: str8) -> i32`

### Runtime Dispatch Hook
- `__mlang_std_jsonrpc_runtime_dispatch(request_payload: str8) -> str8`
- Provided as a weak default in `libmlang_std` (returns empty response).
- Override this symbol in your server program to implement method dispatch.

## std::net

Module file: `stdlib/std/net.mla`

### Types
- `TcpListener`
- `TcpStream`

### Listener API
- `TcpListener::bind(addr: str8, port: i64) -> Result<TcpListener, str8>`
- `TcpListener::accept(self: TcpListener) -> Result<TcpStream, str8>`
- `TcpListener::local_port(self: TcpListener) -> Result<i64, str8>`
- `TcpListener::close(self: TcpListener) -> i32`
- `TcpListener::set_backlog(self: TcpListener, backlog: i64) -> Result<i32, str8>`

### Stream API
- `TcpStream::connect(addr: str8, port: i64) -> Result<TcpStream, str8>`
- `TcpStream::read(self: TcpStream, buf: str8, capacity: i64) -> Result<i64, str8>`
- `TcpStream::write(self: TcpStream, s: str8) -> Result<i64, str8>`
- `TcpStream::close(self: TcpStream) -> i32`
- `TcpStream::set_nonblocking(self: TcpStream, enabled: i32) -> Result<i32, str8>`
- `TcpStream::set_read_timeout_ms(self: TcpStream, timeout_ms: i64) -> Result<i32, str8>`
- `TcpStream::set_write_timeout_ms(self: TcpStream, timeout_ms: i64) -> Result<i32, str8>`
- `TcpStream::try_clone(self: TcpStream) -> Result<TcpStream, str8>`
- `TcpStream::from_handle(handle: i64) -> TcpStream`
- `TcpStream::raw_handle(self: TcpStream) -> i64`

### Errors
- `last_error() -> str8`

## std::printf

Module file: `stdlib/std/printf.mla`

MLang-facing wrappers over C-style output that accept preformatted strings.

### API
- `printf(s: str8) -> void`
- `eprintf(s: str8) -> void`
- `fprintf(fd: i32, s: str8) -> void`

## std::math

Module file: `stdlib/std/math.mla`

### Numeric helpers (overloaded for `i32`, `f32`, `f64`)
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
- `sum_range(start: i32, end: i32) -> i32`
- `factorial(n: i32) -> i32`

## std::algorithm::fft

Module file: `stdlib/std/algorithm/fft.mla`

Radix-2 FFT over interleaved complex arrays:
`[re0, im0, re1, im1, ...]`.

### API
- `is_power_of_two(n: i64) -> bool`
- `forward(data: list<i64>) -> list<i64>` (empty list on invalid input)
- `inverse(data: list<i64>) -> list<i64>` (empty list on invalid input)
- `forward_f32(data: list<f32>) -> list<f32>` (empty list on invalid input)
- `inverse_f32(data: list<f32>) -> list<f32>` (empty list on invalid input)
- `forward_f64(data: list<f64>) -> list<f64>` (empty list on invalid input)
- `inverse_f64(data: list<f64>) -> list<f64>` (empty list on invalid input)

## std::algorithm::order

Module file: `stdlib/std/algorithm/order.mla`

Ordered-sequence helpers for `list<i64>`.

### API
- `is_sorted_i64(data: &list<i64>) -> i32`
- `reverse_i64(data: &list<i64>) -> list<i64>`
- `sort_i64(data: &list<i64>) -> list<i64>`
- `unique_sorted_i64(data: &list<i64>) -> list<i64>`
- `lower_bound_i64(data: &list<i64>, value: i64) -> i64`
- `upper_bound_i64(data: &list<i64>, value: i64) -> i64`
- `binary_search_i64(data: &list<i64>, value: i64) -> i32`

## std::algorithm::numeric

Module file: `stdlib/std/algorithm/numeric.mla`

Numeric sequence helpers for `list<i64>`.

### API
- `accumulate_i64(data: &list<i64>, init: i64) -> i64`
- `partial_sum_i64(data: &list<i64>, init: i64) -> list<i64>`
- `adjacent_difference_i64(data: &list<i64>, init: i64) -> list<i64>`
- `inner_product_i64(a: &list<i64>, b: &list<i64>, init: i64) -> i64`

## std::algorithm::ranges

Module file: `stdlib/std/algorithm/ranges.mla`

C++20 ranges-style helpers for `list<i64>`.

### API
- `find_i64(data: &list<i64>, value: i64) -> i64` (first index or `-1`)
- `count_i64(data: &list<i64>, value: i64) -> i64`
- `contains_i64(data: &list<i64>, value: i64) -> i32`
- `remove_i64(data: &list<i64>, value: i64) -> list<i64>`
- `take_i64(data: &list<i64>, n: i64) -> list<i64>`
- `drop_i64(data: &list<i64>, n: i64) -> list<i64>`
- `unique_i64(data: &list<i64>) -> list<i64>` (adjacent duplicate removal, `std::ranges::unique`-style)
- `unique_stable_i64(data: &list<i64>) -> list<i64>` (full dedup preserving first occurrence order)

## std::process

Module file: `stdlib/std/process.mla`

### Types
- `Child`
- `ChildStdin`
- `ChildStdout`
- `ChildStderr`
- `ExitStatus`
- `WaitPoll`
- `PipeRead`

### Spawn
- `spawn(program: str8, args: list<str8>) -> Result<Child, str8>`
- `spawn_inherit(program: str8, args: list<str8>) -> Result<Child, str8>`
- `last_error() -> str8`

### Child and pipe API
- `Child::stdin(self: Child) -> Result<ChildStdin, str8>`
- `Child::stdout(self: Child) -> Result<ChildStdout, str8>`
- `Child::stderr(self: Child) -> Result<ChildStderr, str8>`
- `Child::wait(self: Child) -> Result<ExitStatus, str8>`
- `Child::try_wait(self: Child) -> Result<WaitPoll, str8>`
- `Child::kill(self: Child, sig: i32) -> Result<i32, str8>`
- `Child::close(self: Child) -> i32`
- `ChildStdin::write(self: ChildStdin, s: str8) -> Result<i64, str8>`
- `ChildStdin::close(self: ChildStdin) -> i32`
- `ChildStdout::read(self: ChildStdout, buf: str8, capacity: i64) -> Result<i64, str8>`
- `ChildStdout::read_nonblocking(self: ChildStdout, buf: str8, capacity: i64) -> Result<PipeRead, str8>`
- `ChildStdout::close(self: ChildStdout) -> i32`
- `ChildStderr::read(self: ChildStderr, buf: str8, capacity: i64) -> Result<i64, str8>`
- `ChildStderr::read_nonblocking(self: ChildStderr, buf: str8, capacity: i64) -> Result<PipeRead, str8>`
- `ChildStderr::close(self: ChildStderr) -> i32`

### Exit status
- `ExitStatus::success(self: ExitStatus) -> i32`
- `ExitStatus::exited(self: ExitStatus) -> i32`
- `ExitStatus::code(self: ExitStatus) -> i32`
- `ExitStatus::signaled(self: ExitStatus) -> i32`
- `ExitStatus::signal(self: ExitStatus) -> i32`

## std::signal

Module file: `stdlib/std/signal.mla`

POSIX-style signal helpers with a runtime dispatcher that both grabs received
signals and forwards them to an installed MLang function handler.

### API
- `last_error() -> str8`
- `on(signum: i32, handler: ptr<void>) -> Result<i32, str8>`
- `ignore(signum: i32) -> Result<i32, str8>`
- `reset_default(signum: i32) -> Result<i32, str8>`
- `raise(signum: i32) -> Result<i32, str8>`
- `received_count(signum: i32) -> i64`
- `clear_received(signum: i32) -> Result<i32, str8>`
- `sigint() -> i32`
- `sigterm() -> i32`
- `sigusr1() -> i32`
- `sigusr2() -> i32`

Example:

```mla
mod std::signal;

var hits: i32 = 0;

fn on_usr1(sig: i32) -> void {
    hits = hits + 1;
}

fn main() -> i32 {
    let sig: i32 = std::signal::sigusr1();
    let _ = std::signal::clear_received(sig);
    let _ = std::signal::on(sig, on_usr1);
    let _ = std::signal::raise(sig);
    let _ = std::signal::reset_default(sig);
    return hits;
}
```

## std::rand

Module file: `stdlib/std/rand.mla`

Pseudo-random helpers (process-global PRNG state):
- `seed(value: i64) -> void`
- `seed_auto() -> i64`
- `next_u64() -> u64`
- `next_i64() -> i64`
- `range_i64(min_value: i64, max_value: i64) -> i64`
- `next_f64() -> f64`
- `range_f64(min_value: f64, max_value: f64) -> f64`

## std::regex

Module file: `stdlib/std/regex.mla`

### Types
- `Regex`

### Compile / lifetime
- `Regex::compile(pattern: str8) -> Result<Regex, str8>`
- `Regex::close(self: Regex) -> i32`

### Matching
- `Regex::is_match(self: Regex, text: str8) -> i32`
- `Regex::find_start(self: Regex, text: str8) -> i64`
- `Regex::find_end(self: Regex, text: str8) -> i64`
- `Regex::match_start(self: Regex, text: str8, group_index: i64) -> i64`
- `Regex::match_end(self: Regex, text: str8, group_index: i64) -> i64`

### Errors
- `last_error() -> str8`

## std::sed

Module file: `stdlib/std/sed.mla`

Sed-like helpers for literal `str8` substitution. These functions do not use
regex syntax; matching is byte-based and exact.

- `replace_first(text: str8, needle: str8, replacement: str8) -> str8`
- `replace_all(text: str8, needle: str8, replacement: str8) -> str8`
- `substitute(text: str8, needle: str8, replacement: str8) -> str8`

Notes:
- each function returns a newly allocated string
- caller frees returned strings with `std::strbuf::free`
- empty `needle` returns a clone of `text`

Example:

```mla
mod std::sed;
mod std::strbuf;

use std::sed::replace_all;
use std::strbuf::free;

let out: str8 = replace_all("name=foo, name=foo", "foo", "bar");
println!("{}", out);
free(out);
```

## std::sync

Module file: `stdlib/std/sync.mla`

### Types
- `Mutex`
- `Condvar`
- `Channel`
- `LockFreeQueue` (SPSC str8 queue)

### Mutex
- `Mutex::new() -> Result<Mutex, str8>`
- `Mutex::lock(self: Mutex) -> Result<i32, str8>`
- `Mutex::unlock(self: Mutex) -> Result<i32, str8>`
- `Mutex::close(self: Mutex) -> i32`

### Condvar
- `Condvar::new() -> Result<Condvar, str8>`
- `Condvar::wait(self: Condvar, mutex: Mutex) -> Result<i32, str8>`
- `Condvar::wait_timeout_ms(self: Condvar, mutex: Mutex, timeout_ms: i64) -> Result<i32, str8>`
- `Condvar::notify_one(self: Condvar) -> Result<i32, str8>`
- `Condvar::notify_all(self: Condvar) -> Result<i32, str8>`
- `Condvar::close(self: Condvar) -> i32`

### Channel (str8)
- `Channel::new(capacity: i64) -> Result<Channel, str8>`
- `Channel::send(self: Channel, s: str8) -> Result<i32, str8>`
- `Channel::post(self: Channel, s: str8) -> Result<i32, str8>` (alias of `send`)
- `Channel::recv(self: Channel, buf: str8, capacity: i64) -> Result<i64, str8>`
- `Channel::try_recv(self: Channel, buf: str8, capacity: i64) -> Result<i64, str8>`
- `Channel::close(self: Channel) -> i32`
- `Channel::free(self: Channel) -> i32`

### LockFreeQueue (str8, single-producer/single-consumer)
- `LockFreeQueue::new(capacity: i64) -> Result<LockFreeQueue, str8>`
- `LockFreeQueue::try_send(self: LockFreeQueue, s: str8) -> Result<i32, str8>`
  - returns `0` on success, `1` when full
- `LockFreeQueue::try_recv(self: LockFreeQueue, buf: str8, capacity: i64) -> Result<i64, str8>`
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
- `term_name() -> str8`
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
- `format_local(pattern: str8) -> str8`
- `local_datetime() -> str8` (`MM/DD/YYYY:HH:MM:SS`)
- `local_datetime_ms() -> str8` (`MM/DD/YYYY:HH:MM:SS.MS`)
- `local_datetime_ns() -> str8` (`MM/DD/YYYY:HH:MM:SS.NS`)

`format_local` token support:
- `YYYY` year
- `DD` day-of-month
- `HH` 24h hour
- `MM` first occurrence = month, following occurrences = minute
- `SS` seconds
- `MS` milliseconds
- `NS` nanoseconds

### Timer
- `Timer::after(timeout_ms: i64) -> Result<Timer, str8>`
- `Timer::reset(self: Timer, timeout_ms: i64) -> i32`
- `Timer::elapsed(self: Timer) -> i32`
- `Timer::remaining_ms(self: Timer) -> i64`
- `Timer::wait(self: Timer) -> i32`
- `Timer::close(self: Timer) -> i32`

## std::timer

Module file: `stdlib/std/timer.mla`

### Types
- `IntervalTimer`
- `AsyncTicker`

### Interval timer API
- `IntervalTimer::every_ms(interval_ms: i64) -> Result<IntervalTimer, str8>`
- `IntervalTimer::reset(self: IntervalTimer) -> i32`
- `IntervalTimer::remaining_ms(self: IntervalTimer) -> i64`
- `IntervalTimer::wait_next(self: IntervalTimer) -> i32`
- `IntervalTimer::poll(self: IntervalTimer) -> i32`
- `IntervalTimer::close(self: IntervalTimer) -> i32`

### Async ticker API
- `AsyncTicker::start(queue_handle: i64, interval_ms: i64, event_name: str8) -> Result<AsyncTicker, str8>`
- `AsyncTicker::stop(self: AsyncTicker) -> i32`
- `AsyncTicker::close(self: AsyncTicker) -> i32`

## std::strbuf

Module file: `stdlib/std/strbuf.mla`

### Allocation
- `String::new() -> str8` (compiler intrinsic wrapper)
- `String::with_capacity(capacity: i64) -> str8` (compiler intrinsic wrapper)
- `String::free(buf: str8) -> void` (compiler intrinsic wrapper)
- `new() -> str8`
- `with_capacity(capacity: i64) -> str8`
- `free(buf: str8) -> void`

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
- `builder_append(builder: StringBuilder, s: str8) -> i64` (bytes appended, `-1` on failure)
- `builder_append_char(builder: StringBuilder, ch: i32) -> i64` (`1` on success, `-1` on failure)
- `builder_to_string(builder: StringBuilder) -> str8` (clone)
- `builder_take_string(builder: StringBuilder) -> str8` (moves out current buffer and resets builder)
- `builder_free(builder: StringBuilder) -> void`

### String helpers
- `len(s: str8) -> i64`
- `is_empty(s: str8) -> i32`
- `clone(s: str8) -> str8`
- `eq(a: str8, b: str8) -> i32`
- `compare(a: str8, b: str8) -> i64`
- `starts_with(s: str8, prefix: str8) -> i32`
- `ends_with(s: str8, suffix: str8) -> i32`
- `contains(s: str8, needle: str8) -> i32`
- `find(s: str8, needle: str8) -> i64`
- `rfind(s: str8, needle: str8) -> i64`
- `concat(a: str8, b: str8) -> str8`
- `repeat(s: str8, count: i64) -> str8`
- `trim(s: str8) -> str8`
- `ltrim(s: str8) -> str8`
- `rtrim(s: str8) -> str8`

### Unicode conversion
- `to_utf16(s: str8) -> str16`
- `from_utf16(s: str16) -> str8`
- `to_utf8(s: str16) -> str8`
- `free_utf16(s: str16) -> void`

## std::bytes

Module file: `stdlib/std/bytes.mla`

### Types
- `Bytes`

### Lifecycle
- `Bytes::new(initial_capacity: i64) -> Result<Bytes, str8>`
- `Bytes::from_string(s: str8) -> Result<Bytes, str8>`
- `Bytes::close(self: Bytes) -> i32`
- `last_error() -> str8`

### Buffer operations
- `Bytes::len(self: Bytes) -> i64`
- `Bytes::capacity(self: Bytes) -> i64`
- `Bytes::clear(self: Bytes) -> i32`
- `Bytes::reserve(self: Bytes, min_capacity: i64) -> i32`
- `Bytes::append_byte(self: Bytes, value: i32) -> i32`
- `Bytes::append_string(self: Bytes, s: str8) -> i64`
- `Bytes::append_bytes(self: Bytes, other: Bytes) -> i64`
- `Bytes::get(self: Bytes, index: i64) -> i32`
- `Bytes::set(self: Bytes, index: i64, value: i32) -> i32`

### Conversions
- `Bytes::to_string(self: Bytes) -> str8` (text-oriented C/UTF-8 str8 copy)
- `Bytes::to_hex(self: Bytes) -> str8` (binary-safe lowercase hex)

## std::serde

Module file: `stdlib/std/serde.mla`

### Types
- `Binary`
- `Reader`
- `BinarySerde` (trait for custom types)

### Global helpers
- `last_error() -> str8`
- `last_ok() -> i32`

### `Binary` API
- `Binary::new(initial_capacity: i64) -> Result<Binary, str8>`
- `Binary::from_file(path: str8) -> Result<Binary, str8>`
- `Binary::len(self: Binary) -> i64`
- `Binary::capacity(self: Binary) -> i64`
- `Binary::clear(self: Binary) -> Result<i32, str8>`
- `Binary::reserve(self: Binary, min_capacity: i64) -> Result<i32, str8>`
- `Binary::write_u8(self: Binary, value: i32) -> Result<i32, str8>`
- `Binary::write_bool(self: Binary, value: bool) -> Result<i32, str8>`
- `Binary::write_i32(self: Binary, value: i32) -> Result<i32, str8>`
- `Binary::write_i64(self: Binary, value: i64) -> Result<i32, str8>`
- `Binary::write_f32(self: Binary, value: f32) -> Result<i32, str8>`
- `Binary::write_f64(self: Binary, value: f64) -> Result<i32, str8>`
- `Binary::write_string(self: Binary, value: str8) -> Result<i32, str8>`
- `Binary::get_u8(self: Binary, index: i64) -> Result<i32, str8>`
- `Binary::to_reader(self: Binary) -> Result<Reader, str8>`
- `Binary::write_file(self: Binary, path: str8) -> Result<i32, str8>`
- `Binary::raw_handle(self: Binary) -> i64`
- `Binary::close(self: Binary) -> i32`

### `Reader` API
- `Reader::from_binary(binary: Binary) -> Result<Reader, str8>`
- `Reader::from_file(path: str8) -> Result<Reader, str8>`
- `Reader::remaining(self: Reader) -> i64`
- `Reader::read_u8(self: Reader) -> Result<i32, str8>`
- `Reader::read_bool(self: Reader) -> Result<bool, str8>`
- `Reader::read_i32(self: Reader) -> Result<i32, str8>`
- `Reader::read_i64(self: Reader) -> Result<i64, str8>`
- `Reader::read_f32(self: Reader) -> Result<f32, str8>`
- `Reader::read_f64(self: Reader) -> Result<f64, str8>`
- `Reader::read_string(self: Reader) -> Result<str8, str8>`
- `Reader::raw_handle(self: Reader) -> i64`
- `Reader::close(self: Reader) -> i32`

### `BinarySerde` trait
- `serialize(self: &mut Self, out_handle: i64) -> Result<i32, str8>`
- `deserialize(self: &mut Self, input_handle: i64) -> Result<i32, str8>`

## std::protocol

Module file: `stdlib/std/protocol.mla`

### Types
- `ProtocolFrame`

### API
- `default_max_payload_bytes() -> i64`
- `last_error() -> str8`
- `connect(addr: str8, port: i64) -> Result<i64, str8>` (protocol stream handle)
- `from_stream(stream: TcpStream) -> i64` (protocol stream handle)
- `send(stream_handle: i64, opcode: i32, payload: str8) -> Result<i64, str8>`
- `recv(stream_handle: i64, payload_capacity: i64, max_payload_bytes: i64) -> Result<ProtocolFrame, str8>`
- `close(stream_handle: i64) -> i32`
- `raw_handle(stream_handle: i64) -> i64`

### Wire format
- magic: `MLP1` (4 bytes)
- opcode: big-endian `u32`
- payload length: big-endian `u32`
- payload bytes

## std::thread

Module file: `stdlib/std/thread.mla`

### Thread and mutex
- `join(handle: Handle<Thread>) -> i32`
- `mutex_new() -> Handle<Mutex>`
- `mutex_lock_handle(handle: Handle<Mutex>) -> i32`
- `mutex_unlock_handle(handle: Handle<Mutex>) -> i32`
- `mutex_free(handle: Handle<Mutex>) -> i32`

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
- `Vec::new_str() -> Vec<str8>` — empty Vec with explicit `str8` element type

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
