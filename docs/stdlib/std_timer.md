# std::timer

Module file: `stdlib/std/timer.mla`

### Types
- `interval_timer`
- `async_ticker`

### Interval timer API
- `interval_timer::every_ms(interval_ms: i64) -> Result<interval_timer, str8>`
- `interval_timer::reset(self: interval_timer) -> i32`
- `interval_timer::remaining_ms(self: interval_timer) -> i64`
- `interval_timer::wait_next(self: interval_timer) -> i32`
- `interval_timer::poll(self: interval_timer) -> i32`
- `interval_timer::close(self: interval_timer) -> i32`

### Async ticker API
- `async_ticker::start(queue_handle: i64, interval_ms: i64, event_name: str8) -> Result<async_ticker, str8>`
- `async_ticker::stop(self: async_ticker) -> i32`
- `async_ticker::close(self: async_ticker) -> i32`
