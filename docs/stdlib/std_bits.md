# std::bits

Module file: `stdlib/std/bits.mla`

The module provides constructors for Mlang's logical `bit` type and C++20–23
inspired operations on unsigned integers and IEEE-754 floating-point
representations. Packed collections remain available in `std::bitset`.

### Logical bit values

- `on()` / `ON()` return `bit(1)`.
- `off()` / `OFF()` return `bit(0)`.

### Unsigned 64-bit operations

- `popcount_u64`, `has_single_bit_u64`, `bit_width_u64`
- `countl_zero_u64`, `countl_one_u64`, `countr_zero_u64`, `countr_one_u64`
- `bit_floor_u64`, `bit_ceil_u64`
- `rotl_u64`, `rotr_u64`, `byteswap_u8`, `byteswap_u16`, `byteswap_u32`,
  `byteswap_u64`

`bit_ceil_u64` returns zero when the next power of two would overflow `u64`.
Rotation counts are normalized modulo 64; negative counts rotate in the
opposite direction.

### Representation-preserving float casts

- `bit_cast_u32_from_f32` and `bit_cast_f32_from_u32`
- `bit_cast_u64_from_f64` and `bit_cast_f64_from_u64`

These reinterpret IEEE-754 bits without performing a numeric conversion. The
native implementation verifies that the target platform uses binary32 and
binary64 layouts.
