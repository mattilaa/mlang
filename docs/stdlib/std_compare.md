# std::compare

Module file: `stdlib/std/compare.mla`

`std::compare` provides a reusable three-way comparison result and relation
helpers inspired by C++20 `<compare>`. Mlang uses one `PartialOrdering` enum
for all generic comparisons rather than separate strong/weak/partial category
types.

```mla
mod std::compare;
use std::compare::*;

let order: PartialOrdering = compare(12, 20);
if is_lt(order) {
    println!("left precedes right");
}
```

### API

- `compare(left, right)` uses `==`, `<`, and `>`; values that satisfy none of
  these relations produce `Unordered` (including floating-point NaNs).
- `compare_by(left, right, less)` uses a strict comparator and returns
  `Equivalent` when neither value precedes the other.
- `is_eq`, `is_neq`, `is_lt`, `is_lteq`, `is_gt`, and `is_gteq` inspect a
  `PartialOrdering`. `is_neq(Unordered)` is true; the other relational helpers
  return false for `Unordered`.

`compare` is intended for types whose equality and ordering operators form a
consistent relation. Use `compare_by` when a custom strict ordering defines
the comparison domain.
