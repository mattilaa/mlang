# std::event_loop

Module file: `stdlib/std/event_loop.mla`

### Types
- `EventLoop`

### API
- `EventLoop::start(queue_handle: i64, interval_ms: i64, event_name: str8) -> Result<EventLoop, str8>`
- `EventLoop::stop(self: EventLoop) -> i32`
- `EventLoop::close(self: EventLoop) -> i32`
