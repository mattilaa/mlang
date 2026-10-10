# `std::chrono`

Module file: `stdlib/std/chrono.mla`

Checked integral durations with explicit runtime units, inspired by C++20
`std::chrono::duration`. Mlang currently uses a `Duration { count, unit }`
value instead of a compile-time period parameter. Supported units range from
nanoseconds through weeks.

```mla
mod std::chrono;
use std::chrono::Duration;
use std::chrono::DurationUnit;
use std::chrono::make_duration;
use std::chrono::duration_cast;
use std::chrono::duration_floor;
use std::chrono::duration_ceil;
use std::chrono::duration_round;
use std::chrono::duration_add;
use std::chrono::duration_compare;

let elapsed = make_duration(90, DurationUnit::Second).unwrap();
let milliseconds: Duration =
    duration_cast(elapsed, DurationUnit::Millisecond).unwrap();
let total: Duration = duration_add(
    elapsed, make_duration(2, DurationUnit::Minute).unwrap(),
    DurationUnit::Second).unwrap(); // 210 seconds
let order = duration_compare(elapsed, total).unwrap(); // Less
let rounded_minutes: Duration =
    duration_round(elapsed, DurationUnit::Minute).unwrap(); // 2 minutes
```

- `make_duration(count, unit)` constructs a value for one of the declared
  `DurationUnit` alternatives.
- `duration_cast(value, unit)` converts to the requested unit. Conversions to
  coarser units truncate toward zero, like integral C++ `duration_cast`;
  conversions that overflow `i64` return `None`.
- `duration_floor(value, unit)`, `duration_ceil(value, unit)`, and
  `duration_round(value, unit)` provide C++ chrono rounding. `floor` and
  `ceil` handle negative fractions toward negative and positive infinity,
  respectively; `round` selects the nearest count and sends exact ties to the
  even count. Unrepresentable conversions return `None`.
- `duration_add(left, right, unit)` and `duration_subtract(left, right, unit)`
  convert both operands to the requested unit before checked arithmetic. A
  conversion or arithmetic overflow returns `None`.
- `duration_compare(left, right)` returns `Some(DurationOrder::Less)`,
  `Some(DurationOrder::Equivalent)`, or `Some(DurationOrder::Greater)` for
  exact cross-unit ordering. It avoids multiplying
  full counts by conversion factors, so even extreme `i64` values can be
  compared without overflow.

`DurationUnit` contains `Nanosecond`, `Microsecond`, `Millisecond`, `Second`,
`Minute`, `Hour`, `Day`, and `Week`. The module deliberately reports overflow
through `option` rather than wrapping counts.
