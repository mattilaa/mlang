# std::vec

Module file: `stdlib/std/vec.mla`

`Vec<T>` is a type alias for `list<T>`. The two are interchangeable in all
contexts. Methods listed below are compiler intrinsics backed by `libmlang_std`
and are also summarized in "Built-in Collection Methods (Compiler Intrinsics)"
above. The free `erase` and range-modifier helpers are generic library
operations for in-place Vec updates.

### Constructors

- `new() -> list<T>` — create an empty vector using the declared element type

### Macros

- `vec![a, b, c]` — construct a Vec from a comma-separated list of elements
- `vec![val; N]` — construct a Vec of `N` copies of `val`

### Size

- `v.len() -> i64` — number of elements currently stored
- `v.is_empty() -> bool` — `true` when the Vec contains no elements

### Mutation

- `v.push(val)` — append `val` to the end (grows the Vec by one)
- `v.pop() -> T` — remove and return the last element; known-empty arrays are
  rejected at compile time, otherwise empty containers abort at runtime
- `v.clear()` — remove all elements (Vec remains valid for further pushes)
- `erase(v, value) -> i64` — remove all matching values (C++20
  `std::erase(vector, value)`); returns the number removed
- `erase_if(v, predicate) -> i64` — remove values accepted by a predicate
  (C++20 `std::erase_if(vector, predicate)`); preserves survivor order and
  returns the number removed
- `append_range(&mut v, source) -> i64` — append a snapshot of `source`
  (C++23 `vector::append_range`); returns the appended count
- `assign_range(&mut v, source) -> i64` — replace all values from a snapshot
  (C++23 `vector::assign_range`); returns the assigned count
- `insert_range(&mut v, index, source) -> i64` — insert a snapshot at a
  clamped index (C++23 `vector::insert_range`); returns the actual index
- `set_f32(values, index, value) -> i32` — replace one `f32` element without
  changing list length or allocating; returns `-1` for an invalid index

### Search

- `v.contains(val) -> bool` — `true` if `val` is present in the Vec
- `v.index_of(val) -> i64` — zero-based index of first match, or `-1` if absent

### Ordering

- `v.sort()` — sort elements in ascending order (in-place)
- `v.sort_desc()` — sort elements in descending order (in-place)
- `v.reverse()` — reverse element order (in-place)
- `v.dedup()` — remove consecutive duplicate elements; sort first to deduplicate all

### Access

- `v.first() -> T` — return the first element; known-empty arrays are rejected
  at compile time, otherwise emptiness is checked before loading
- `v.last() -> T` — return the last element; known-empty arrays are rejected at
  compile time, otherwise emptiness is checked before loading

### Iteration

```mlang
// for-in loop
for x in v {
    println!("{}", x);
}

// enumerated loop
for(i, x) in v.enumerate() {
    println!("v[{}] = {}", i, x);
}
```
