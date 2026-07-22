# std::net

Module file: `stdlib/std/net.mla`

### Types
- `tcp_listener`
- `tcp_stream`

### Listener API
- `tcp_listener::bind(addr: str8, port: i64) -> Result<tcp_listener, str8>`
- `tcp_listener::accept(self: tcp_listener) -> Result<tcp_stream, str8>`
- `tcp_listener::local_port(self: tcp_listener) -> Result<i64, str8>`
- `tcp_listener::close(self: tcp_listener) -> i32`
- `tcp_listener::set_backlog(self: tcp_listener, backlog: i64) -> Result<i32, str8>`

### Stream API
- `tcp_stream::connect(addr: str8, port: i64) -> Result<tcp_stream, str8>`
- `tcp_stream::read(self: tcp_stream, buf: str8, capacity: i64) -> Result<i64, str8>`
- `tcp_stream::write(self: tcp_stream, s: str8) -> Result<i64, str8>`
- `tcp_stream::close(self: tcp_stream) -> i32`
- `tcp_stream::set_nonblocking(self: tcp_stream, enabled: i32) -> Result<i32, str8>`
- `tcp_stream::set_read_timeout_ms(self: tcp_stream, timeout_ms: i64) -> Result<i32, str8>`
- `tcp_stream::set_write_timeout_ms(self: tcp_stream, timeout_ms: i64) -> Result<i32, str8>`
- `tcp_stream::try_clone(self: tcp_stream) -> Result<tcp_stream, str8>`
- `tcp_stream::from_handle(handle: i64) -> tcp_stream`
- `tcp_stream::raw_handle(self: tcp_stream) -> i64`

### Errors
- `last_error() -> str8`
