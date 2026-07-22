# std::io

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
