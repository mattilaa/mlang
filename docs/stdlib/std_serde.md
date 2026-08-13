# std::serde

Module file: `stdlib/std/serde.mla`

### Types
- `binary`
- `reader`
- `BinarySerde` (trait for custom types)

### Global helpers
- `last_error() -> str8`
- `last_ok() -> i32`

### `binary` API
- `binary::new(initial_capacity: i64) -> result<binary, str8>`
- `binary::from_file(path: str8) -> result<binary, str8>`
- `binary::len(self: binary) -> i64`
- `binary::capacity(self: binary) -> i64`
- `binary::clear(self: binary) -> result<i32, str8>`
- `binary::reserve(self: binary, min_capacity: i64) -> result<i32, str8>`
- `binary::write_u8(self: binary, value: i32) -> result<i32, str8>`
- `binary::write_bool(self: binary, value: bool) -> result<i32, str8>`
- `binary::write_i32(self: binary, value: i32) -> result<i32, str8>`
- `binary::write_i64(self: binary, value: i64) -> result<i32, str8>`
- `binary::write_f32(self: binary, value: f32) -> result<i32, str8>`
- `binary::write_f64(self: binary, value: f64) -> result<i32, str8>`
- `binary::write_string(self: binary, value: str8) -> result<i32, str8>`
- `binary::get_u8(self: binary, index: i64) -> result<i32, str8>`
- `binary::to_reader(self: binary) -> result<reader, str8>`
- `binary::write_file(self: binary, path: str8) -> result<i32, str8>`
- `binary::raw_handle(self: binary) -> i64`
- `binary::close(self: binary) -> i32`

### `reader` API
- `reader::from_binary(binary: binary) -> result<reader, str8>`
- `reader::from_file(path: str8) -> result<reader, str8>`
- `reader::remaining(self: reader) -> i64`
- `reader::read_u8(self: reader) -> result<i32, str8>`
- `reader::read_bool(self: reader) -> result<bool, str8>`
- `reader::read_i32(self: reader) -> result<i32, str8>`
- `reader::read_i64(self: reader) -> result<i64, str8>`
- `reader::read_f32(self: reader) -> result<f32, str8>`
- `reader::read_f64(self: reader) -> result<f64, str8>`
- `reader::read_string(self: reader) -> result<str8, str8>`
- `reader::raw_handle(self: reader) -> i64`
- `reader::close(self: reader) -> i32`

### `BinarySerde` trait
- `serialize(self: &mut Self, out_handle: i64) -> result<i32, str8>`
- `deserialize(self: &mut Self, input_handle: i64) -> result<i32, str8>`
