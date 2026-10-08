# std::algorithm::numeric

Module file: `stdlib/std/algorithm/numeric.mla`

Numeric sequence helpers for integer lists, including generic list folds.

### API
- `accumulate_i64(data: &list<i64>, init: i64) -> i64`
- `fold_left(data, init, operation)` applies `operation(accumulator, element)`
  from the first element to the last; it returns `init` for empty input.
- `fold_right(data, init, operation)` applies `operation(element, accumulator)`
  from the last element to the first; it returns `init` for empty input.
- `partial_sum_i64(data: &list<i64>, init: i64) -> list<i64>`
- `adjacent_difference_i64(data: &list<i64>, init: i64) -> list<i64>`
- `inner_product_i64(a: &list<i64>, b: &list<i64>, init: i64) -> i64`
