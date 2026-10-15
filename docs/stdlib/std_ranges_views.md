# std::ranges::views

Module file: `stdlib/std/ranges/views.mla`

This module provides indexable lazy views over an owned `list<T>`:

- `take(source, count)` exposes at most the first `count` items.
- `drop(source, count)` exposes the suffix after skipping up to `count` items.

Both return view values with `len()`, checked `get(index) -> option<T>`, and
`collect() -> list<T>`. `get` reads from the owned source without copying the
whole selected range; `collect` explicitly materializes a new list. Negative
counts act as zero. Counts beyond the source length are clamped.

These are owning views because Mlang does not yet have lifetime-parameterized
borrowed views. Passing a list into an adapter transfers it to the view.
If the original list must remain usable, explicitly copy its storage first
with `std::copy(source)`; this is supported when the list element type is
`Copy`. Copies are never inserted implicitly just to keep the source alive.

```mlang
mod std::ranges::views;
use std::ranges::views::*;

fn main() -> i32 {
    let source: list<i64> = [10, 20, 30, 40];
    let first_three = take(std::copy(source), 3);
    println!("{} {}", first_three.len(), first_three.get(1).unwrap());
    let last_two = drop([10, 20, 30, 40], 2);
    let values: list<i64> = last_two.collect();
    println!("{} {}", values[0], values[1]);
    return 0;
}
```
