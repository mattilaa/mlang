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
use std::algorithm::ranges::filter;

fn example() {
    let data: list<i32> = [4, 2, 4, 7];
    let first_four: i64 = find(data, 4);           // 0
    let prefix: list<i32> = take(data, 2);        // [4, 2]
    let distinct: list<i32> = unique_stable(data); // [4, 2, 7]
    let positive = |value: i32| { return value > 0; };
    let positives: list<i32> = filter(data, positive); // [4, 2, 4, 7]
}
```

### Search and comparison

- `find`, `find_last`, `adjacent_find`, `count`, and `contains` search for a
  value; `adjacent_find` returns the first index of equal neighbors or `-1`.
- `find_last_if` and `find_last_if_not` return the last index accepted or
  rejected by a predicate, respectively, or `-1` when no element matches.
- `adjacent_find_by(data, equivalent)` returns the first adjacent index whose
  pair satisfies a caller-provided binary equivalence predicate, or `-1`.
- `find_first_of(data, candidates)` returns the first index in `data` matching
  any candidate, or `-1` when there is no match.
- `includes(range, subset)` checks whether sorted `range` contains sorted
  `subset`; duplicate multiplicities matter and both inputs must be sorted.
- `search` returns the first index of a contiguous pattern (`0` for an empty
  pattern, `-1` if absent); `contains_subrange` returns the corresponding
  membership result.
- `find_end` returns the last matching subrange start (`data.len()` for an
  empty pattern, `-1` if absent).
- `search_by` and `find_end_by` find the first or last contiguous subrange
  matching under a caller-provided equivalence predicate.
- `equal` compares lists in order, including across distinct equality-comparable
  element types; `is_permutation` compares element multiplicities without
  considering order.
- `equal_by` compares each pair with a supplied equivalence predicate, while
  `is_permutation_by` compares multiplicities under that equivalence.
- `lexicographical_compare` supports ranges with distinct mutually
  order-comparable element types.
- `lexicographical_compare_by` performs the same range comparison with a
  caller-supplied strict ordering; equivalent elements continue to the next
  position and a matching prefix sorts before its extension.
- `includes_by(range, subset, less)` checks sorted-range containment with a
  comparator, including duplicate multiplicities.
- `starts_with` and `ends_with` compare prefixes and suffixes, including across
  distinct equality-comparable element types.
- `lexicographical_compare`, `is_sorted`, `min_element_index`, and
  `max_element_index` provide ordering queries. The min/max index functions
  return `-1` on an empty list and keep the first index on ties.
- `min_element_value` and `max_element_value` return the selected value for a
  nonempty input. Use the index forms to handle empty ranges; value forms keep
  the first value on ties.
- The corresponding `min_element_index_by`, `max_element_index_by`,
  `min_element_value_by`, and `max_element_value_by` forms accept a strict
  ordering comparator and also keep the first equivalent element.
- `is_sorted_until(data)` returns the first index that breaks nondecreasing
  order, or `data.len()` when the entire range is sorted.
- `is_heap(data)` checks max-heap order; `is_heap_until(data)` returns the
  first violating child index, or `data.len()` when the range is a heap.
- `is_heap_by(data, less)` and `is_heap_until_by(data, less)` check heap order
  using a strict comparator; the returned index is the first child for which
  `less(parent, child)` is true.
- `lower_bound`, `upper_bound`, and `binary_search` perform logarithmic-time
  queries on a list sorted in nondecreasing order. The bounds return insertion
  indices; binary search returns `bool`.
- `is_sorted_by`, `is_sorted_until_by`, `lower_bound_by`, `upper_bound_by`, and
  `binary_search_by` accept a strict ordering comparator, enabling descending or
  custom orderings while keeping binary searches logarithmic.
- `size` and `empty` provide generic list size queries.
- `find_if` and `count_if` search/count elements accepted by a bound predicate;
  `filter` eagerly copies accepted values to a new list. Predicates may capture
  local values, and captured mutations are visible to the caller.
- `find_if_not` returns the first rejected element's index; `remove_if` returns
  a copy without accepted elements, and `replace_if` substitutes a value for
  every accepted element. These transforms leave their input unchanged.
- `unique_by(data, equivalent)` removes adjacent equivalent elements while
  keeping the first value in each run.
- `all_of`, `any_of`, and `none_of` accept predicates as well as retaining the
  one-argument `list<bool>` identity forms. Empty-range results match the
  standard algorithms: true, false, and true respectively.
- `is_partitioned` checks whether all matching elements precede nonmatches;
  `partition_point` returns the first nonmatch index for an already-partitioned
  range. `stable_partition` returns a copied range with matching elements
  first, preserving order within both groups and evaluating its predicate once
  per element.

### Transforms and sequence creation

- `remove`, `take`, `drop`, `unique`, `unique_stable`, and `clamp` return a
  transformed copy. `unique` removes only adjacent duplicates;
  `unique_stable` keeps the first occurrence of each value.
- `clamp_by(data, low, high, less)` clamps each element using a custom ordering;
  bounds must be ordered according to the comparator.
- `sorted` returns a stable ascending copy and leaves the input unchanged.
- `sorted_by(data, less)` returns a stable copied ordering by a strict
  comparator. Equivalent elements retain their original relative order.
- `reversed` returns the elements in reverse order; `rotate_left` returns a
  copy rotated by a normalized signed offset.
- `chunked(data, size)` materializes consecutive sublists (C++23
  `views::chunk` style); the final chunk can be shorter, and non-positive sizes
  produce an empty list.
- `windows(data, size)` materializes every overlapping fixed-size sublist
  (C++23 `views::slide` style); invalid sizes produce an empty list.
- `strided(data, step)` copies every `step`-th element from index zero
  (C++23 `views::stride` style); non-positive steps produce an empty list.
- `joined(data)` flattens one list-of-lists level into a copied list (C++20
  `views::join` style); empty inner lists contribute no elements.
- `concat(left, right)` copies two same-typed ranges into one list (C++23
  `views::concat` style).
- `zip(left, right)` materializes tuple pairs from two ranges, stopping at the
  shorter input (C++23 `views::zip` style).
- `merged(left, right)` stably merges two sorted same-typed ranges into a new
  sorted list; equal elements from the left range come first.
- `union_sorted(left, right)` returns the sorted multiset union, keeping the
  maximum duplicate count for every value.
- `intersection_sorted(left, right)` returns the sorted multiset intersection,
  keeping the minimum duplicate count for every value.
- `difference_sorted(left, right)` removes matching right-side occurrences
  from the left sorted range, preserving unmatched duplicate counts.
- `symmetric_difference_sorted(left, right)` returns values present in exactly
  one sorted input, with each value's output count equal to the difference in
  input multiplicities.
- `merged_by`, `union_sorted_by`, `intersection_sorted_by`,
  `difference_sorted_by`, and `symmetric_difference_sorted_by` take a strict
  ordering comparator. Both inputs must be sorted with that comparator; set
  operations define equivalent values as neither being less than the other.
  They preserve the same stability and duplicate-count rules as their default
  ordering counterparts.
- `replace(data, old, new)` copies a range while replacing every value equal
  to `old`; the original range is unchanged.
- `iota(start, end)` materializes the half-open integer interval `[start, end)`;
  it returns an empty list when `start >= end`.

The module retains the original `_i64` functions for source compatibility.
