# std::bitset

Module file: `stdlib/std/bitset.mla`

Packed dynamic bitset container for dense one-bit-per-entry storage.

Use `std::bitset::BitSet` when a boolean collection must be compact or when you
want packed bit operations through `std::simd`.

Examples:
- `examples/std_simd_demo.mla`
- `tests/std_bitset_tests.mla`
- `tests/std_simd_bitset_tests.mla`

### Types
- `BitSet`

### API
- `last_error() -> str8`
- `BitSet::new(bit_capacity: i64) -> Result<BitSet, str8>`
- `BitSet::len(self: BitSet) -> i64`
- `BitSet::capacity(self: BitSet) -> i64`
- `BitSet::clear(self: BitSet) -> i32`
- `BitSet::resize(self: BitSet, new_len: i64, fill: bool) -> i32`
- `BitSet::set(self: BitSet, index: i64, value: bool) -> i32`
- `BitSet::set_fast(self: BitSet, index: i64, value: bool) -> i32`
- `BitSet::get(self: BitSet, index: i64) -> Result<bool, str8>`
- `BitSet::get_fast(self: BitSet, index: i64) -> i32`
- `BitSet::toggle(self: BitSet, index: i64) -> i32`
- `BitSet::push(self: BitSet, value: bool) -> i32`
- `BitSet::pop(self: BitSet) -> Result<bool, str8>`
- `BitSet::count_ones(self: BitSet) -> i64`
- `BitSet::and_eq(self: BitSet, rhs_handle: i64) -> i32`
- `BitSet::or_eq(self: BitSet, rhs_handle: i64) -> i32`
- `BitSet::xor_eq(self: BitSet, rhs_handle: i64) -> i32`
- `BitSet::not_eq(self: BitSet) -> i32`
- `BitSet::raw_handle(self: BitSet) -> i64`
- `BitSet::close(self: BitSet) -> i32`

### Notes
- `BitSet::len()` is measured in bits.
- `list<bool>` is a normal list container, not a packed specialization.
- `std::simd` provides packed bitset reductions and in-place bitwise helpers.
