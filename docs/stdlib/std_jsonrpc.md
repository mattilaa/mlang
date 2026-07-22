# std::jsonrpc

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
