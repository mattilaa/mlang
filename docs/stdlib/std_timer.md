# std::timer

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
