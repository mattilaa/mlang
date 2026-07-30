# std::log

Module file: `stdlib/std/log.mla`

Line-oriented logging helpers:
- `out(msg: str8) -> i64` writes `msg` plus newline to stdout.
- `warn(msg: str8) -> i64` writes `warn: {msg}` plus newline to stderr.
- `err(msg: str8) -> i64` writes `err: {msg}` plus newline to stderr.

Created loggers:
- `Log::new() -> Log` creates a console-only logger.
- `Log::with_output_path(path: str8) -> Result<Log, str8>` creates a logger
  with file forwarding defined up front.
- `logger.out(msg)`, `logger.warn(msg)`, `logger.err(msg)`
- `logger.set_output_path(path)`, `logger.reset_output_path()`,
  `logger.close()`

File forwarding:
- `set_output_path(path: str8) -> i32`
- `set_out_path(path: str8) -> i32`
- `set_warn_path(path: str8) -> i32`
- `set_err_path(path: str8) -> i32`
- `reset_output_path() -> i32`
- `forward_all_to_file(path: str8) -> i32`
- `forward_out_to_file(path: str8) -> i32`
- `forward_warn_to_file(path: str8) -> i32`
- `forward_err_to_file(path: str8) -> i32`
- `clear_forwarding() -> i32`

The default is console-only logging. Setting a path appends to that file and
keeps writing to the normal console stream. Setting another path changes the
destination for future messages. Call `reset_output_path()` or
`clear_forwarding()` before exit when you want to close the files explicitly.

```mlang
mod std::log;
use std::log::*;

fn main() -> i32 {
    let lr: Result<Log, str8> = Log::with_output_path("created.log");
    if lr.is_err() { return 1; }
    let logger: Log = lr.unwrap();
    logger.out("this logger was created with a path");
    logger.close();

    set_output_path("app.log");
    out("started");
    warn("using defaults");
    err("request failed");
    set_output_path("next.log");
    out("future messages go to next.log");
    reset_output_path();
    return 0;
}
```
