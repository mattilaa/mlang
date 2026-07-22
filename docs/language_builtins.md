# Language Built-ins

Compiler-provided collection methods, enum behavior, aliases, and builtin type helpers.

## Built-in Collection Methods (Compiler Intrinsics)

These methods are language/compiler intrinsics and are available directly on
collection/str8 values (including values typed through `use type` aliases).

## Built-in Enum Behavior

Enum declarations define named values and may specify an explicit integer
backing type such as `u8` or `i64`, or `str8` for string-backed enums.

- Enum values format/print as `EnumName::Variant`.
- Integer-backed enums accept decimal, binary, and hex integer literals.
- Unknown integer-backed enum values print as `<EnumName:unknown>`.
- `str8`-backed enums compare directly with `str8`.
- `for value in Status { ... }` iterates the enum in declaration order.
- `for (i, value) in Status.enumerate() { ... }` iterates in declaration order
  and exposes a zero-based `i64` index.

Example:

```mla
enum Status : u8 {
    Invalid = 1,
    Busy = 2,
    Success = 3
};

for value in Status {
    println!("{}", value);
}

for (i, value) in Status.enumerate() {
    println!("[{}] {}", i, value);
}
```

String-backed example:

```mla
enum HttpMethod : str8 {
    Get = "GET",
    Post = "POST",
    Patch = "PATCH",
};

let method: HttpMethod = HttpMethod::Post;
verify_eq("POST", method);
```

### `str8`
`String` in `String::new/with_capacity/free` is a compiler wrapper namespace
for this builtin type, not a distinct type declaration.

- `s.len() -> i64`
- `s.is_empty() -> i32`

### `list<T>` / `Vec<T>`

`Vec<T>` is a type alias for `list<T>`, so both expose the same method surface.

- `v.len() -> i64`
- `v.is_empty() -> bool`
- `v.push(value) -> void`
- `v.pop() -> T` (checked non-empty)
- `v.clear() -> void`
- `v.contains(value) -> bool`
- `v.index_of(value) -> i64`
- `v.sort() -> void`
- `v.sort_desc() -> void`
- `v.reverse() -> void`
- `v.dedup() -> void`
- `v.first() -> T`
- `v.last() -> T`

### `map<K, V>`
- `m.len() -> i64`
- `m[key] -> V`; aborts with `map key not found` if the key is absent
- `m.keys()` iterator for `for key in m.keys() { ... }`
- `m.values()` iterator for `for val in m.values() { ... }`
- `m.entries()` iterator for `for entry in m.entries() { ... }`
  where `entry` is a tuple `(K, V)` and can be accessed via `.0` / `.1`

## Type Aliases (`alias` / `use type`)

`alias` lets you introduce readable names for builtin or generic types and the alias respects lexical scope.
These declarations can appear at the top level **or** inside blocks—the alias disappears once the block exits.
Aliases also support generics, so `alias SomeMap<K, V> = map<K, V>;` is equivalent to the C++ style `using`.
The older `use type SomeMap<K, V> = map<K, V>;` spelling remains valid and equivalent; prefer `alias` for ordinary type alias declarations.

Example:

```mla
alias Distance = f32;
alias SomeMap = map<str8, i32>;

let scores: SomeMap = {"Alice": 95, "Bob": 87};

{
    alias Distance = i32;
    let grid: Distance = 42;
    println!("grid={}", grid);
}
// outside block `Distance` still refers to `f32`
```

If an alias name is duplicated in the same scope, the compiler reports an error referencing the original definition (`file.mla:row:column: alias 'Distance' already defined`), so you can reliably locate the conflict.

Associated calls and instance method lookup resolve through aliases, so an alias
can be used as a real type name in calls such as `mutex::new()` and on variables
typed as `mutex`.

## Builtin `bit` and `size_of`

Builtin reference source: `stdlib/types.mla`

### `bit`
- `bit` is a builtin logical 0/1 type
- Use `bit(expr)` to convert an integer or bool expression
- `size_of(bit)` reports the ABI byte size

### `size_of`
- `size_of(Type) -> i64`
- `size_of(expr) -> i64`
- Returns the ABI byte size in bytes; for `array<T, N>`, returns
  `N * size_of(T)`
- Can be used in `static_assert!` when the target size is known at compile time

Examples:

```mla
var enabled: bit = 1;
println!("bit={} bool={} list_header={}",
         size_of(bit), size_of(bool), size_of(list<bool>));
static_assert!(size_of(enabled) == size_of(bit));
static_assert!(size_of(array<i32, 6>) == 24);
```
