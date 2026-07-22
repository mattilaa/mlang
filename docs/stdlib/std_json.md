# std::json

Module file: `stdlib/std/json.mla`

Compiler-synthesized JSON serde is also available for supported structs via
`#[derive(Json)]`. This generates:
- `value.to_json() -> str8`
- `StructName::from_json(text) -> Result<StructName, str8>`

For derived structs, base fields are emitted directly in the JSON object. For
fields declared with `@property(...)`, serialization also emits a sibling
`@property` metadata object describing the active property flags.

### Types
- `JsonDoc`
- `JsonValue`
- `JsonArrayIter`
- `JsonObjectIter`

### Kind constants
- `kind_invalid()`
- `kind_null()`
- `kind_bool()`
- `kind_number()`
- `kind_string()`
- `kind_array()`
- `kind_object()`

### Document API
- `JsonDoc::parse(text: str8) -> Result<JsonDoc, str8>`
- `JsonDoc::from_file(path: str8) -> Result<JsonDoc, str8>`
- `JsonDoc::root(self: JsonDoc) -> Result<JsonValue, str8>`
- `JsonDoc::stringify(self: JsonDoc) -> Result<str8, str8>`
- `JsonDoc::stringify_pretty(self: JsonDoc) -> Result<str8, str8>`
- `JsonDoc::free(self: JsonDoc) -> void`
- `last_error() -> str8`

### Value API
- `JsonValue::kind(self: JsonValue) -> i32`
- `JsonValue::size(self: JsonValue) -> Result<i64, str8>`
- `JsonValue::get(self: JsonValue, key: str8) -> Result<JsonValue, str8>`
- `JsonValue::index(self: JsonValue, i: i64) -> Result<JsonValue, str8>`
- `JsonValue::as_bool(self: JsonValue) -> Result<i32, str8>`
- `JsonValue::as_i64(self: JsonValue) -> Result<i64, str8>`
- `JsonValue::as_f64(self: JsonValue) -> Result<f64, str8>`
- `JsonValue::as_string(self: JsonValue) -> Result<str8, str8>`
- `JsonValue::key_at(self: JsonValue, i: i64) -> Result<str8, str8>`
- `JsonValue::iter_array(self: JsonValue) -> Result<JsonArrayIter, str8>`
- `JsonValue::iter_object(self: JsonValue) -> Result<JsonObjectIter, str8>`
- `JsonValue::free(self: JsonValue) -> void`

### Iterator API
- `JsonArrayIter::has_next(self: JsonArrayIter) -> i32`
- `JsonArrayIter::current(self: JsonArrayIter) -> Result<JsonValue, str8>`
- `JsonArrayIter::advance(self: JsonArrayIter) -> JsonArrayIter`
- `JsonObjectIter::has_next(self: JsonObjectIter) -> i32`
- `JsonObjectIter::current_key(self: JsonObjectIter) -> Result<str8, str8>`
- `JsonObjectIter::current_value(self: JsonObjectIter) -> Result<JsonValue, str8>`
- `JsonObjectIter::advance(self: JsonObjectIter) -> JsonObjectIter`

### `#[derive(Json)]` decode support
- `bool`
- signed and unsigned integer primitives
- `f32`
- `f64`
- `str8`
- nested structs that also derive `Json`
