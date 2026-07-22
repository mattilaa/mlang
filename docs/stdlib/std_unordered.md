# std::unordered

Module file: `stdlib/std/unordered.mla`

Hash-backed runtime containers:
- `HashMapI64I64`
  - `new()`, `close()`
  - `insert(key, value)`, `contains(key)`, `get_or(key, default_value)`
  - `remove(key)`, `len()`, `keys()`, `values()`
- `QuickMapI64I64` (convenience API over `HashMapI64I64`)
  - `new()`, `close()`
  - `set(key, value)`, `get(key, fallback)`, `has(key)`, `del(key)`
  - `len()`, `keys()`, `values()`
- `QuickMapVecI64I64` (vector-backed convenience map)
  - `new()`, `close()`
  - `set(key, value)`, `get(key, fallback)`, `has(key)`, `del(key)`
  - `len()`, `keys()`, `values()`
- `HashSetI64`
  - `new()`, `close()`
  - `insert(key)`, `contains(key)`, `remove(key)`, `len()`, `keys()`

Compatibility wrapper structs (builtin-backed):
- `UnorderedMap<K, V> { data: map<K, V> }`
- `UnorderedSet<T> { data: list<T> }`
- `Vector<T> { data: list<T> }`
