# std::log

Module file: `stdlib/std/log.mla`

Line-oriented logging helpers:
- `out(msg: str8) -> i64` writes `msg` plus newline to stdout.
- `warn(msg: str8) -> i64` writes `warn: {msg}` plus newline to stderr.
- `err(msg: str8) -> i64` writes `err: {msg}` plus newline to stderr.

Created loggers:
- `Log::new() -> Log` creates a console-only logger.
- `Log::with_output_path(path: str8) -> result<Log, str8>` creates a logger
  with file forwarding defined up front.
- `Log::with_options(path: str8, timestamps_enabled: i32) -> result<Log, str8>`
  creates a logger with file forwarding and timestamps defined up front.
- `logger.out(msg)`, `logger.warn(msg)`, `logger.err(msg)`
- `logger.set_output_path(path)`, `logger.reset_output_path()`,
  `logger.enable_timestamps()`, `logger.disable_timestamps()`,
  `logger.set_timestamp_colors_enabled(enabled)`,
  `logger.set_timestamp_color(level, ansi)`,
  `logger.set_timestamp_color_code(level, code)`, `logger.close()`

File forwarding:
- `set_output_path(path: str8) -> i32`
- `set_out_path(path: str8) -> i32`
- `set_warn_path(path: str8) -> i32`
- `set_err_path(path: str8) -> i32`
- `reset_output_path() -> i32`
- `enable_timestamps() -> i32`
- `disable_timestamps() -> i32`
- `set_timestamps_enabled(enabled: i32) -> i32`
- `set_timestamp_colors_enabled(enabled: i32) -> i32`
- `set_timestamp_color(level: i32, ansi: str8) -> i32`
- `set_timestamp_color_code(level: i32, code: i32) -> i32`
- `forward_all_to_file(path: str8) -> i32`
- `forward_out_to_file(path: str8) -> i32`
- `forward_warn_to_file(path: str8) -> i32`
- `forward_err_to_file(path: str8) -> i32`
- `clear_forwarding() -> i32`

The default is console-only logging. Setting a path appends to that file and
keeps writing to the normal console stream. Setting another path changes the
destination for future messages. Call `reset_output_path()` or
`clear_forwarding()` before exit when you want to close the files explicitly.
Timestamps are off by default. When enabled, each line is prefixed with
`[M/D/YYYY/HH:MM:SS] `. Timestamp colors are on by default when timestamps are
enabled: green for `out`, yellow for `warn`, red for `err`; the message text is
normal white. Disable colors with `set_timestamp_colors_enabled(0)` or override
per-level colors with `set_timestamp_color_code(level_out(), 36)`.

```mlang
mod std::log;
use std::log::*;

fn main() -> i32 {
    let lr: result<Log, str8> = Log::with_options("created.log", 1);
    if lr.is_err() { return 1; }
    let logger: Log = lr.unwrap();
    logger.out("this logger was created with a path and timestamps");
    logger.set_timestamp_color_code(level_warn(), 35);
    logger.warn("this warning has a magenta timestamp");
    logger.close();

    set_output_path("app.log");
    enable_timestamps();
    out("started");
    warn("using defaults");
    err("request failed");
    set_timestamp_colors_enabled(0);
    out("timestamp without color");
    set_output_path("next.log");
    out("future messages go to next.log");
    disable_timestamps();
    reset_output_path();
    return 0;
}
```
