# std::span

Module file: `stdlib/std/span.mla`

C++20-style span aliases over the existing safe list runtime shape. Because
these are type aliases for `list<T>`, they have list ownership/lifetime
semantics and do not provide zero-copy, lifetime-tracked C++ span views.

- `Span<T>` is a compiler alias for `list<T>`
- `span<T>` is the lowercase alias for the same type

Properties:
- `size_of(Span<T>) == size_of(list<T>)`
- indexing uses the same compile-time and runtime bounds checks as `list<T>`
- values can be initialized from normal lists, `Vec<T>`, and array-fill forms
  like `[value; N]`
- `size_of(spanValue)` is accepted in `static_assert!` when the span value type
  is known at compile time
- `size(data)` and `empty(data)` query the alias
- `size_bytes(data)` returns `size(data) * size_of(T)`, matching the C++20
  accessor; `get(data, index)` is a Mlang safe extension returning `Some` for
  an in-bounds element and `None` otherwise
- `first(data, count)`, `last(data, count)`, and `subspan(data, offset, count)`
  return copied lists, not non-owning subviews. Counts are safely clamped;
  negative `first`/`last` counts return empty, and a negative `subspan` count
  means through the end.

Example:

```mla
mod std::span;

fn sum(values: Span<i32>) -> i32 {
    var total: i32 = 0;
    for i in 0..values.len() {
        total = total + values[i];
    }
    return total;
}

static_assert!(size_of(Span<i32>) == size_of(list<i32>));
let view: Span<i32> = [1, 2, 3];
static_assert!(size_of(view) == size_of(list<i32>));
```
