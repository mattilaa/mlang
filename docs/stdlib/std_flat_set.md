# `std::flat_set`

`std::flat_set` is a C++23-inspired sorted associative set backed by contiguous
Mlang list storage. It stores unique keys in ascending order, uses binary
search for reads, and rebuilds its list for updates, like the sibling
[`std::flat_map`](std_flat_map.md).

```mlang
mod std::flat_set;
use std::flat_set::FlatSet;

var ids: FlatSet<i32> = FlatSet<i32>::new();
ids.insert(42);
ids.insert(7);
let smallest: i32 = ids.key_at(0); // 7
```

`FlatSet<T>` and lowercase alias `flat_set<T>` provide `new`, `len`,
`is_empty`, `lower_bound`, `upper_bound`, `equal_range`, `contains`, `insert`,
`insert_range`, `replace`, `remove`, `clear`, and `key_at`. Free functions
`erase_if` and `merge` provide C++20/23-style bulk operations.

Ordering equivalence means neither key is less than the other. `insert` returns
true only for a new key. `insert_range` processes values in order and returns
the number added. `replace` accepts only strictly increasing keys and leaves
the set unchanged if validation fails. `erase_if` preserves sorted order and
returns the removed count. `merge(destination, source)` transfers only keys
not already present in the destination; collisions remain in the source, and
the function returns the number transferred.

`key_at` requires an index in `[0, len())`. Lookup is O(log n); insertion,
removal, replacement, and range insertion are O(n) or worse due to list
reconstruction. This implementation is aimed at compact key sets with
read-heavy workloads rather than frequent updates.
