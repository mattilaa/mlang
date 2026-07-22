# std::protocol

Module file: `stdlib/std/protocol.mla`

### Types
- `protocol_frame`

### API
- `default_max_payload_bytes() -> i64`
- `last_error() -> str8`
- `connect(addr: str8, port: i64) -> Result<i64, str8>` (protocol stream handle)
- `from_stream(stream: tcp_stream) -> i64` (protocol stream handle)
- `send(stream_handle: i64, opcode: i32, payload: str8) -> Result<i64, str8>`
- `recv(stream_handle: i64, payload_capacity: i64, max_payload_bytes: i64) -> Result<protocol_frame, str8>`
- `close(stream_handle: i64) -> i32`
- `raw_handle(stream_handle: i64) -> i64`

### Wire format
- magic: `MLP1` (4 bytes)
- opcode: big-endian `u32`
- payload length: big-endian `u32`
- payload bytes
