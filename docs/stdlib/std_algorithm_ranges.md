# std::algorithm::ranges

Module file: `stdlib/std/algorithm/ranges.mla`

Generic C++20–23-inspired algorithms for Mlang lists. Equality and ordering
algorithms are monomorphized for the element type; searching supports a
separate key type when Mlang can compare the values. Container transforms
return a new list and leave the input unchanged.

```mla
mod std::algorithm::ranges;
use std::algorithm::ranges::find;
use std::algorithm::ranges::take;
use std::algorithm::ranges::unique_stable;

fn example() {
    let data: list<i32> = [4, 2, 4, 7];
    let first_four: i64 = find(data, 4);           // 0
    let prefix: list<i32> = take(data, 2);        // [4, 2]
    let distinct: list<i32> = unique_stable(data); // [4, 2, 7]
}
```

### Search and comparison

- `find`, `find_last`, `count`, and `contains` search for a value.
- `search` returns the first index of a contiguous pattern (`0` for an empty
  pattern, `-1` if absent); `contains_subrange` returns the corresponding
  membership result.
- `equal` compares lists in order; `is_permutation` compares element
  multiplicities without considering order.
- `starts_with` and `ends_with` compare prefixes and suffixes.
- `lexicographical_compare`, `is_sorted`, `min_element_index`, and
  `max_element_index` provide ordering queries. The min/max index functions
  return `-1` on an empty list and keep the first index on ties.
- `lower_bound`, `upper_bound`, and `binary_search` perform logarithmic-time
  queries on a list sorted in nondecreasing order. The bounds return insertion
  indices; binary search returns `bool`.
- `size` and `empty` provide generic list size queries.

### Transforms and sequence creation

- `remove`, `take`, `drop`, `unique`, `unique_stable`, and `clamp` return a
  transformed copy. `unique` removes only adjacent duplicates;
  `unique_stable` keeps the first occurrence of each value.
- `sorted` returns a stable ascending copy and leaves the input unchanged.
- `reversed` returns the elements in reverse order; `rotate_left` returns a
  copy rotated by a normalized signed offset.
- `chunked(data, size)` materializes consecutive sublists (C++23
  `views::chunk` style); the final chunk can be shorter, and non-positive sizes
  produce an empty list.
- `windows(data, size)` materializes every overlapping fixed-size sublist
  (C++23 `views::slide` style); invalid sizes produce an empty list.
- `strided(data, step)` copies every `step`-th element from index zero
  (C++23 `views::stride` style); non-positive steps produce an empty list.
- `iota(start, end)` materializes the half-open integer interval `[start, end)`;
  it returns an empty list when `start >= end`.

The module retains the original `_i64` functions for source compatibility.
