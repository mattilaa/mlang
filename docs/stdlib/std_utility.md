# `std::utility`

Module file: `stdlib/std/utility.mla`

`std::utility` exposes small value-level helpers inspired by the C++ utility
library.

```mla
mod std::utility;
use std::utility::to_underlying;
use std::utility::exchange;

enum Status : u8 { Ready = 1, Failed = 255 };
let raw: u8 = to_underlying(Status::Failed);
var current: i32 = 12;
let previous: i32 = exchange(current, 20); // previous = 12, current = 20
```

- `exchange(value, replacement)` replaces a mutable value and returns the old
  contents. Mlang currently requires the replacement to have the same type as
  the value.
- `to_underlying(value)` converts an integer-backed enum to an integer. Since
  Mlang does not currently express an enum's underlying type as an associated
  type, provide the matching destination type through the result's expected
  type (for example, `let raw: u8 = to_underlying(value)`).
- `cmp_equal_*`, `cmp_not_equal_*`, `cmp_less_*`, `cmp_greater_*`,
  `cmp_less_equal_*`, and `cmp_greater_equal_*` compare signed and unsigned
  integer operands without converting a negative signed value or a large
  unsigned value through the wrong domain. The suffix identifies operand
  order: `_signed_unsigned` or `_unsigned_signed`. Inputs are widened losslessly
  to `i64` and `u64`, so smaller integer types are supported too.
- `in_range_<target>_from_<source>(value)` checks whether a value can be
  represented by the named integer target (`i8`, `i16`, `i32`, `i64`, `u8`,
  `u16`, `u32`, or `u64`) before a cast. All eight source widths are supported;
  narrower values are widened without loss before checking, so callers do not
  need to cast to `i64` or `u64` themselves.

This follows the behavior of C++23 `std::to_underlying` when the destination
type matches the enum's declared underlying type. Mixed-sign comparison and
conversion range helpers follow C++20 `<utility>` integer comparison
(`cmp_*`) and `in_range` semantics.
