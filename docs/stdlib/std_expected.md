# std::expected

Module file: `stdlib/std/expected.mla`

Monadic transformations for Mlang's built-in `result<T, E>`, following the
C++23 `std::expected` interface. All operations return new results and leave
their input unchanged.

```mla
mod std::expected;
use std::expected::transform;
use std::expected::transform_error;
use std::expected::value_or;
use std::expected::value_or_else;

fn example() {
    let parsed: result<i32, str8> = Ok<i32, str8>(21);
    let double = |value: i32| { return value * 2; };
    let doubled: result<i32, str8> = transform(parsed, double);
    let defaulted: i32 = value_or(doubled, 0);
    // doubled is Ok(42); Err values bypass the closure unchanged.
    let failed: result<i32, str8> = Err<i32, str8>("missing");
    let fallback = || { return i32(0); };
    let recovered: i32 = value_or_else(failed, fallback);
}
```

### API

- `transform(input, operation)` maps the successful value to a new type while
  preserving the error type. The operation is called only for `Ok`.
- `transform_error(input, operation)` maps the error to a new type while
  preserving the successful value type. The operation is called only for `Err`.
- `value_or(input, fallback)` returns the value for `Ok`, or the supplied value
  for `Err`. As in C++23 `expected::value_or`, the fallback expression is
  evaluated before the call.
- `value_or_else(input, fallback)` has the same result but lazily invokes its
  zero-argument closure only for `Err` (a convenience beyond the C++23 API).

These are free functions because Mlang's `result<T, E>` is a built-in generic
sum type rather than an importable class. They provide the value/error mapping
semantics of C++23 `expected::transform` and `expected::transform_error`.
