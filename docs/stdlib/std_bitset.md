# std::bitset

Module file: `stdlib/std/bitset.mla`

Packed dynamic bitset container for dense one-bit-per-entry storage.

Use `std::bitset::bit_set` when a boolean collection must be compact or when you
want packed bit operations through `std::simd`.

Examples:
- `examples/std_simd_demo.mla`
- `tests/std_bitset_tests.mla`
- `tests/std_simd_bitset_tests.mla`

### Types
- `bit_set`

### API
- `last_error() -> str8`
- `bit_set::new(bit_capacity: i64) -> Result<bit_set, str8>`
- `bit_set::len(self: bit_set) -> i64`
- `bit_set::capacity(self: bit_set) -> i64`
- `bit_set::clear(self: bit_set) -> i32`
- `bit_set::resize(self: bit_set, new_len: i64, fill: bool) -> i32`
- `bit_set::set(self: bit_set, index: i64, value: bool) -> i32`
- `bit_set::set_fast(self: bit_set, index: i64, value: bool) -> i32`
- `bit_set::get(self: bit_set, index: i64) -> Result<bool, str8>`
- `bit_set::get_fast(self: bit_set, index: i64) -> i32`
- `bit_set::toggle(self: bit_set, index: i64) -> i32`
- `bit_set::push(self: bit_set, value: bool) -> i32`
- `bit_set::pop(self: bit_set) -> Result<bool, str8>`
- `bit_set::count_ones(self: bit_set) -> i64`
- `bit_set::and_eq(self: bit_set, rhs_handle: i64) -> i32`
- `bit_set::or_eq(self: bit_set, rhs_handle: i64) -> i32`
- `bit_set::xor_eq(self: bit_set, rhs_handle: i64) -> i32`
- `bit_set::not_eq(self: bit_set) -> i32`
- `bit_set::raw_handle(self: bit_set) -> i64`
- `bit_set::close(self: bit_set) -> i32`

### Notes
- `bit_set::len()` is measured in bits.
- `list<bool>` is a normal list container, not a packed specialization.
- `std::simd` provides packed bitset reductions and in-place bitwise helpers.
