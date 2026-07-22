# std::bytes

Module file: `stdlib/std/bytes.mla`

### Types
- `Bytes`

### Lifecycle
- `Bytes::new(initial_capacity: i64) -> Result<Bytes, str8>`
- `Bytes::from_string(s: str8) -> Result<Bytes, str8>`
- `Bytes::close(self: Bytes) -> i32`
- `last_error() -> str8`

### Buffer operations
- `Bytes::len(self: Bytes) -> i64`
- `Bytes::capacity(self: Bytes) -> i64`
- `Bytes::clear(self: Bytes) -> i32`
- `Bytes::reserve(self: Bytes, min_capacity: i64) -> i32`
- `Bytes::append_byte(self: Bytes, value: i32) -> i32`
- `Bytes::append_string(self: Bytes, s: str8) -> i64`
- `Bytes::append_bytes(self: Bytes, other: Bytes) -> i64`
- `Bytes::get(self: Bytes, index: i64) -> i32`
- `Bytes::set(self: Bytes, index: i64, value: i32) -> i32`

### Conversions
- `Bytes::to_string(self: Bytes) -> str8` (text-oriented C/UTF-8 str8 copy)
- `Bytes::to_hex(self: Bytes) -> str8` (binary-safe lowercase hex)
