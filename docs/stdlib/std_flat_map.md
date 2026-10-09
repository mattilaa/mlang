# `std::flat_map`

`std::flat_map` provides a sorted associative map backed by contiguous list
storage. Keys are unique and iteration-by-index is in ascending key order.
This is an eager Mlang container: insertion and removal rebuild the backing
list, while lookup uses binary search.

```mlang
mod std::flat_map;
use std::flat_map::FlatMap;

var scores: FlatMap<i32, str8> = FlatMap<i32, str8>::new();
scores.insert(20, "twenty");
scores.insert(10, "ten");
let first_key: i32 = scores.key_at(0);       // 10
let first_value: str8 = scores.value_at(0);  // "ten"
let score: option<str8> = scores.get(20);
```

The API includes `len`, `is_empty`, `lower_bound`, `upper_bound`, `equal_range`,
`contains`, `get`, `insert`, `insert_range`, `remove`, `clear`, `key_at`, and
`value_at`.
`equal_range(key)` returns the half-open index pair `(lower_bound, upper_bound)`;
it is empty for an absent key and contains one entry for a present key because
keys are unique. `insert` returns true
for a new key and false when replacing an existing key's value. `remove`
returns whether the key existed. Index access requires `0 <= index < len()`.
`insert_range(entries)` processes entries in order, returns the count of newly
created distinct keys, and lets the last occurrence of a repeated key determine
its value.

Keys must support ordering (`<`) and equality (`==`). Lookup is O(log n);
insertion and removal are O(n) due to list reconstruction. `insert_range` is
sequential, so inserting `m` entries into an `n`-entry map is O(m(n + m)) in
the worst case. Prefer this type for small maps or workloads with frequent
lookup and relatively few updates.
