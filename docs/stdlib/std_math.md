# std::math

Module file: `stdlib/std/math.mla`

### Numeric helpers (overloaded for `i32`, `f32`, `f64`)
- `add(a, b)`
- `subtract(a, b)`
- `multiply(a, b)`
- `square(x)`
- `abs(x)`
- `min(a, b)`
- `max(a, b)`
- `clamp(x, low, high)`
- `pow(a, b)`
- `sqrt(x)`
- `hypot(x, y)` for `f32` and `f64`, computing a stable Euclidean norm without
  squaring large or tiny arguments directly (C++20 `std::hypot`)
- `fma(x, y, z)` for `f32` and `f64`, computing `x*y+z` with one final
  rounding (C++ `std::fma`)
- `nextafter(x, y)`, `isfinite(x)`, `isinf(x)`, `isnan(x)`, and `signbit(x)`
  for both floating types, exposing representable-neighbor and IEEE-754
  classification operations from `<cmath>`
- `sin(x)`
- `cos(x)`
- `tan(x)`
- `floor(x)`
- `ceil(x)`
- `round(x)`
- `log(x)`
- `exp(x)`
- `modulo(a, b)`
- `lerp(a, b, t)` for `f32` and `f64`, with exact endpoint results and
  overflow-aware interpolation across opposite-sign endpoints (C++20 `std::lerp`)
- `midpoint(a, b)` for `i32`, `i64`, `f32`, and `f64`; integer results round
  toward the first argument without signed overflow (C++20 `std::midpoint`)

### Integer-specific
- `sum_range(start: i32, end: i32) -> i32`
- `factorial(n: i32) -> i32`
