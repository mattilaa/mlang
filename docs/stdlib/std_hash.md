# std::hash

Module file: `stdlib/std/hash.mla`

Stable 64-bit hashing helpers for cache keys, fingerprints, and combined IDs.

### Free functions
- `init() -> i64`
- `hash_i64(value: i64) -> i64`
- `hash_bool(value: bool) -> i64`
- `hash_str(text: str8) -> i64`
- `hash_str16(text: str16) -> i64`
- `combine(seed: i64, value_hash: i64) -> i64`
- `to_hex(value: i64) -> str8`

### Incremental builder
- `hasher::new() -> hasher`
- `hasher::write_i64(self: &mut Self, value: i64) -> void`
- `hasher::write_bool(self: &mut Self, value: bool) -> void`
- `hasher::write_str(self: &mut Self, text: str8) -> void`
- `hasher::write_str16(self: &mut Self, text: str16) -> void`
- `hasher::write_hash(self: &mut Self, value_hash: i64) -> void`
- `hasher::finish(self: hasher) -> i64`
- `hasher::finish_hex(self: hasher) -> str8`

Example:

```mla
mod std::hash;
use std::hash::*;

var h: hasher = hasher::new();
h.write_str("device");
h.write_i64(42);
println!("{}", h.finish_hex());
```

