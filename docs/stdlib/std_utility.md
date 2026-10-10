# `std::utility`

Module file: `stdlib/std/utility.mla`

`std::utility` exposes small value-level helpers inspired by the C++ utility
library.

```mla
mod std::utility;
use std::utility::to_underlying;

enum Status : u8 { Ready = 1, Failed = 255 };
let raw: u8 = to_underlying(Status::Failed);
```

- `to_underlying(value)` converts an integer-backed enum to an integer. Since
  Mlang does not currently express an enum's underlying type as an associated
  type, provide the matching destination type through the result's expected
  type (for example, `let raw: u8 = to_underlying(value)`).

This follows the behavior of C++23 `std::to_underlying` when the destination
type matches the enum's declared underlying type.
