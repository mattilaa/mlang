# std::ranges::views

Module file: `stdlib/std/ranges/views.mla`

The initial view adapters are lazy, indexable prefix and suffix views over an
owned `list<T>`:

- `take(source, count)` limits the visible prefix.
- `drop(source, count)` skips the initial items.

Views provide `len()`, checked `get(index) -> option<T>`, and `collect()` to
explicitly materialize a list. Negative counts mean zero; excessive counts
are clamped. Adapters own the source list because Mlang does not yet support
lifetime-parameterized borrowed views.

```mlang
mod std::ranges::views;
use std::ranges::views::*;

let prefix = take([10, 20, 30, 40], 3);
let suffix: list<i64> = drop([10, 20, 30, 40], 2).collect();
```
