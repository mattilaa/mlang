# `std::flat_multimap`

`std::flat_multimap` is a C++23-inspired sorted associative multimap backed by
contiguous list storage. It retains equivalent keys and preserves insertion
order within each equivalent-key group. Reads use binary search; updates
rebuild the backing list.

```mlang
mod std::flat_multimap;
use std::flat_multimap::FlatMultiMap;

var headers: FlatMultiMap<i32, str8> = FlatMultiMap<i32, str8>::new();
headers.insert(7, "first");
headers.insert(7, "second");
let count: i64 = headers.count(7); // 2
```

`FlatMultiMap<K,V>` and lowercase alias `flat_multimap<K,V>` provide `new`,
`len`, `is_empty`, `lower_bound`, `upper_bound`, `equal_range`, `count`,
`contains`, `get`, `insert`, `insert_range`, `replace`, `remove`, `clear`,
`key_at`, and `value_at`. Free `erase_if` and `merge` functions provide bulk
operations.

Ordering equivalence means neither key is less than the other. `get(key)`
returns the first value in the equivalent-key group, or `None`. `insert` places
new equivalent entries after existing ones; `remove(key)` erases the entire
group and returns its size. `replace` accepts nondecreasing keys and equal
key/value lengths, leaving the map unchanged when validation fails.
`merge(destination, source)` transfers all pairs, including equivalent keys,
and empties the source. `erase_if` passes each key and value to its predicate
and preserves the remaining order.

Index access requires `0 <= index < len()`. Lookup is O(log n); updates are
O(n) or worse from list reconstruction, making this best for compact,
read-heavy maps.
