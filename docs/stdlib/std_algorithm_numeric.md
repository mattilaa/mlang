# std::algorithm::numeric

Module file: `stdlib/std/algorithm/numeric.mla`

Numeric sequence helpers for integer lists, including generic list folds.

### API
- `midpoint_i64(first, second)` and `midpoint_u64(first, second)` compute an
  overflow-safe integral midpoint, rounding a tie toward `first` (C++20
  `std::midpoint`). `midpoint_f32` and `midpoint_f64` provide overflow-safe
  floating-point midpoint overloads for the corresponding Mlang types.
  `midpoint_i8/i16/i32` and `midpoint_u8/u16/u32` cover the remaining integer
  widths with the same tie behavior.
- `accumulate_i64(data: &list<i64>, init: i64) -> i64`
- `accumulate_by(data, init, operation)` folds with a caller-supplied
  `operation(accumulator, element)`; the accumulator type may differ from the
  list element type, and empty input returns `init`.
- `fold_left(data, init, operation)` applies `operation(accumulator, element)`
  from the first element to the last; it returns `init` for empty input.
- `fold_right(data, init, operation)` applies `operation(element, accumulator)`
  from the last element to the first; it returns `init` for empty input.
- `fold_left_first(data, operation)` and `fold_right_last(data, operation)` fold
  without an initial value and return `option<T>` (`None` for empty input,
  otherwise `Some(result)`), matching the C++23 ranges folds.
- `transform_reduce(data, init, reduce, project)` projects each element and
  reduces it in one pass. Its two-range overload transforms corresponding pairs
  and stops at the shorter input.
- `inclusive_scan(data, operation)` returns each accumulated prefix, beginning
  with the first input value. `exclusive_scan(data, init, operation)` emits the
  accumulator before incorporating each element; its accumulator/output type
  may differ from the input element type. Empty inputs yield empty lists.
- `inclusive_scan(data, init, operation)` is the seeded inclusive form; it
  combines the initial accumulator with each input before emitting that prefix.
  Its accumulator/output type may differ from the input element type.
- `transform_inclusive_scan` projects each input before accumulation, with
  overloads that initialize from the first projected element or from an
  explicit initial value. `transform_exclusive_scan` emits the accumulator
  before incorporating each projected input. Projection, accumulator, and
  input element types can differ.
- `partial_sum_i64(data: &list<i64>, init: i64) -> list<i64>`
- `adjacent_difference_i64(data: &list<i64>, init: i64) -> list<i64>`
- `partial_sum_by(data, operation)` performs a custom inclusive fold, starting
  with the first input; the operation receives `(prefix, current)`.
- `adjacent_difference_by(data, operation)` starts with the first input and
  computes later values as `operation(current, previous)`, as in C++ numeric
  algorithms. Both return empty output for empty input and preserve the input
  element type.
- `inner_product_i64(a: &list<i64>, b: &list<i64>, init: i64) -> i64`
- `inner_product(left, right, init)` computes the generic sum of pairwise
  products over the shared prefix and permits distinct input element types.
- `inner_product_by(left, right, init, reduce, product)` generalizes the
  operation pair with caller-provided closures, supporting heterogeneous input
  lists and accumulator types.
