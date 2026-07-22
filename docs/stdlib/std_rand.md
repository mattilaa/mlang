# std::rand

Module file: `stdlib/std/rand.mla`

Pseudo-random helpers (process-global PRNG state):
- `seed(value: i64) -> void`
- `seed_auto() -> i64`
- `next_u64() -> u64`
- `next_i64() -> i64`
- `range_i64(min_value: i64, max_value: i64) -> i64`
- `next_f64() -> f64`
- `range_f64(min_value: f64, max_value: f64) -> f64`
