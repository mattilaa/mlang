# `std::numbers`

Module file: `stdlib/std/numbers.mla`

C++20 `<numbers>` constants, exposed as zero-argument `f64` functions. C++23
additions `egamma()` and `phi()` are included as well. Names use descriptive
snake case for fractional constants.

```mla
mod std::numbers;
use std::numbers::pi;
use std::numbers::two_pi;
use std::numbers::pi_over_2;

fn circle_area(radius: f64) -> f64 {
    return pi() * radius * radius;
}
```

The module provides `e`, `ln2`, `ln10`, `log2e`, `log10e`, `pi`, `two_pi`,
`pi_over_2`, `pi_over_4`, `inv_pi`, `two_inv_pi`, `inv_sqrtpi`,
`two_inv_sqrtpi`, `sqrt2`, `inv_sqrt2`, `sqrt3`, and `inv_sqrt3`. The C++23
constants `egamma` (Euler–Mascheroni) and `phi` (golden ratio) are also
available.
