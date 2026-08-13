# std::json

Module file: `stdlib/std/json.mla`

Compiler-synthesized JSON serde is also available for supported structs via
`#[derive(Json)]`. This generates:
- `value.to_json() -> str8`
- a lowercase associated `from_json(text)` constructor on the derived struct,
  returning `result<your_struct, str8>`

For derived structs, base fields are emitted directly in the JSON object. For
fields declared with `@property(...)`, serialization also emits a sibling
`@property` metadata object describing the active property flags.

### Types
- `json_doc`
- `json_value`
- `json_array_iter`
- `json_object_iter`

### Kind constants
- `kind_invalid()`
- `kind_null()`
- `kind_bool()`
- `kind_number()`
- `kind_string()`
- `kind_array()`
- `kind_object()`

### Document API
- `json_doc::parse(text: str8) -> result<json_doc, str8>`
- `json_doc::from_file(path: str8) -> result<json_doc, str8>`
- `json_doc::root(self: json_doc) -> result<json_value, str8>`
- `json_doc::stringify(self: json_doc) -> result<str8, str8>`
- `json_doc::stringify_pretty(self: json_doc) -> result<str8, str8>`
- `json_doc::free(self: json_doc) -> void`
- `last_error() -> str8`

### Value API
- `json_value::kind(self: json_value) -> i32`
- `json_value::size(self: json_value) -> result<i64, str8>`
- `json_value::get(self: json_value, key: str8) -> result<json_value, str8>`
- `json_value::index(self: json_value, i: i64) -> result<json_value, str8>`
- `json_value::as_bool(self: json_value) -> result<i32, str8>`
- `json_value::as_i64(self: json_value) -> result<i64, str8>`
- `json_value::as_f64(self: json_value) -> result<f64, str8>`
- `json_value::as_string(self: json_value) -> result<str8, str8>`
- `json_value::key_at(self: json_value, i: i64) -> result<str8, str8>`
- `json_value::iter_array(self: json_value) -> result<json_array_iter, str8>`
- `json_value::iter_object(self: json_value) -> result<json_object_iter, str8>`
- `json_value::free(self: json_value) -> void`

### Iterator API
- `json_array_iter::has_next(self: json_array_iter) -> i32`
- `json_array_iter::current(self: json_array_iter) -> result<json_value, str8>`
- `json_array_iter::advance(self: json_array_iter) -> json_array_iter`
- `json_object_iter::has_next(self: json_object_iter) -> i32`
- `json_object_iter::current_key(self: json_object_iter) -> result<str8, str8>`
- `json_object_iter::current_value(self: json_object_iter) -> result<json_value, str8>`
- `json_object_iter::advance(self: json_object_iter) -> json_object_iter`

### `#[derive(Json)]` decode support
- `bool`
- signed and unsigned integer primitives
- `f32`
- `f64`
- `str8`
- nested structs that also derive `Json`
