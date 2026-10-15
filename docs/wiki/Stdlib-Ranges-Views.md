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

If the original list must remain usable, explicitly use `std::copy(source)`
before passing it to an owning adapter. This allocates a separate list buffer
only when the element type is `Copy`. Elements follow their ordinary `Copy`
semantics: pointer-like values, strings, and pointer fields still refer to the
same targets. It is not a recursive clone, and Mlang has no general `Clone`
trait or user-defined copy hook yet.

```mlang
mod std::ranges::views;
use std::ranges::views::*;

let source: list<i64> = [10, 20, 30, 40];
let prefix = take(std::copy(source), 3);
let suffix: list<i64> = drop([10, 20, 30, 40], 2).collect();
```
