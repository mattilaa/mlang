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
- `bit_set::new(bit_capacity: i64) -> result<bit_set, str8>`
- `bit_set::len(self: bit_set) -> i64`
- `bit_set::capacity(self: bit_set) -> i64`
- `bit_set::clear(self: bit_set) -> i32`
- `bit_set::resize(self: bit_set, new_len: i64, fill: bool) -> i32`
- `bit_set::set(self: bit_set, index: i64, value: bool) -> i32`
- `bit_set::set_fast(self: bit_set, index: i64, value: bool) -> i32`
- `bit_set::get(self: bit_set, index: i64) -> result<bool, str8>`
- `bit_set::get_fast(self: bit_set, index: i64) -> i32`
- `bit_set::toggle(self: bit_set, index: i64) -> i32`
- `bit_set::push(self: bit_set, value: bool) -> i32`
- `bit_set::pop(self: bit_set) -> result<bool, str8>`
- `bit_set::count_ones(self: bit_set) -> i64`
- `bit_set::count(self: bit_set) -> i64` (alias for `count_ones`)
- `bit_set::set_all(self: bit_set) -> i32`
- `bit_set::reset(self: bit_set) -> i32` (preserves length; unlike `clear`)
- `bit_set::flip_all(self: bit_set) -> i32`
- `bit_set::shift_left(self: bit_set, amount: i64) -> i32`
- `bit_set::shift_right(self: bit_set, amount: i64) -> i32`
- `bit_set::any(self: bit_set) -> bool`
- `bit_set::none(self: bit_set) -> bool`
- `bit_set::all(self: bit_set) -> bool` (true for an empty bitset)
- `bit_set::and_eq(self: bit_set, rhs_handle: i64) -> i32`
- `bit_set::or_eq(self: bit_set, rhs_handle: i64) -> i32`
- `bit_set::xor_eq(self: bit_set, rhs_handle: i64) -> i32`
- `bit_set::not_eq(self: bit_set) -> i32`
- `bit_set::bit_and(self: bit_set, other: bit_set) -> result<bit_set, str8>`
- `bit_set::bit_or(self: bit_set, other: bit_set) -> result<bit_set, str8>`
- `bit_set::bit_xor(self: bit_set, other: bit_set) -> result<bit_set, str8>`
- `bit_set::complement(self: bit_set) -> result<bit_set, str8>`
- `bit_set::raw_handle(self: bit_set) -> i64`
- `bit_set::close(self: bit_set) -> i32`

### Notes
- `bit_set::len()` is measured in bits.
- Shifts preserve the bitset length and discard bits shifted beyond either end.
  Negative shift amounts return `-1`; amounts at least `len()` clear all bits.
- Value-returning bitwise operations allocate an independent bitset and leave
  both inputs unchanged. Binary operations require equal lengths.
- `list<bool>` is a normal list container, not a packed specialization.
- `std::simd` provides packed bitset reductions and in-place bitwise helpers.
