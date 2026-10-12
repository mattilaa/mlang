# std::type_traits

Module file: `stdlib/std/type_traits.mla`

Compile-time traits for primitive types, inspired by C++20 `<type_traits>`.
All queries are generic `cexpr fn`s and must be evaluated in a compile-time
context.

```mla
mod std::type_traits;
use std::type_traits::is_integral;
use std::type_traits::is_same;

static_assert!(cexpr(is_integral(42)));
static_assert!(cexpr(is_same(1i32, 2i32)));
static_assert!(!cexpr(is_integral(2.5)));
```

### API

- `is_same(left, right)` checks whether the two argument types are identical.
- `is_integral(value)` recognizes `bool`, `bit`, and the signed/unsigned
  8-, 16-, 32-, and 64-bit integer types.
- `is_floating_point(value)` recognizes `f32` and `f64`.
- `is_signed(value)` recognizes signed integer and floating-point types.
- `is_unsigned(value)` recognizes unsigned integer types and `bool`.
- `is_arithmetic(value)` combines integral and floating-point classification.
- `is_enum(value)` recognizes declared enum types at compile time.
- `is_scoped_enum(value)` is the C++23 trait; Mlang enums are always scoped,
  so it has the same result as `is_enum`.

The argument supplies a type for generic inference; the trait itself is
evaluated at compile time and does not inspect the argument's runtime value.
Enum classification is based on compiler enum declarations, not on the enum's
integer backing type. Pointer, array, and additional user-defined class traits
are not yet provided.
