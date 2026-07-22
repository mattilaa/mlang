# std::time

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

### timer
- `timer::after(timeout_ms: i64) -> Result<timer, str8>`
- `timer::reset(self: timer, timeout_ms: i64) -> i32`
- `timer::elapsed(self: timer) -> i32`
- `timer::remaining_ms(self: timer) -> i64`
- `timer::wait(self: timer) -> i32`
- `timer::close(self: timer) -> i32`

