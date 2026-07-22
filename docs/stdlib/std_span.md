# std::span

Module file: `stdlib/std/span.mla`

C++20-style non-owning span/view aliases over the existing safe list runtime
shape.

- `Span<T>` is a compiler alias for `list<T>`
- `span<T>` is the lowercase alias for the same type

Properties:
- `size_of(Span<T>) == size_of(list<T>)`
- indexing uses the same compile-time and runtime bounds checks as `list<T>`
- values can be initialized from normal lists, `Vec<T>`, and array-fill forms
  like `[value; N]`
- `size_of(spanValue)` is accepted in `static_assert!` when the span value type
  is known at compile time

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
