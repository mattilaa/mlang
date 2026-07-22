# std::event_loop

Module file: `stdlib/std/event_loop.mla`

### Types
- `event_loop`

### API
- `event_loop::start(queue_handle: i64, interval_ms: i64, event_name: str8) -> Result<event_loop, str8>`
- `event_loop::stop(self: event_loop) -> i32`
- `event_loop::close(self: event_loop) -> i32`
