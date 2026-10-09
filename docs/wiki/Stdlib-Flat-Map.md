# `std::flat_map`

See [`std::flat_map` API documentation](../stdlib/std_flat_map.md).

This sorted, list-backed associative map has unique keys, binary-search lookup,
and index-based access in key order. Insertions and removals rebuild the list,
so it best fits small maps and read-heavy workloads. `insert_range` processes
key/value entries in order, with the last duplicate supplying the stored value.
`erase_if(map, predicate)` removes matching key/value pairs and preserves the
sorted order of all remaining entries.
