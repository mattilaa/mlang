# std::expected

Module file: `stdlib/std/expected.mla`

Monadic transformations for Mlang's built-in `result<T, E>`, following the
C++23 `std::expected` interface. All operations return new results and leave
their input unchanged.

```mla
mod std::expected;
use std::expected::transform;
use std::expected::transform_error;
use std::expected::and_then;
use std::expected::or_else;
use std::expected::value_or;
use std::expected::value_or_else;

fn example() {
    let parsed: result<i32, str8> = Ok<i32, str8>(21);
    let double = |value: i32| { return value * 2; };
    let doubled: result<i32, str8> = transform(parsed, double);
    let zero: i32 = 0;
    let defaulted: i32 = value_or(doubled, zero);
    // doubled is Ok(42); Err values bypass the closure unchanged.
    let failed: result<i32, str8> = Err<i32, str8>("missing");
    let fallback = || { return i32(0); };
    let recovered: i32 = value_or_else(failed, fallback);
    let recover = |message: str8| { return Ok<i32, bool>(0); };
    let recovered_result: result<i32, bool> = or_else(failed, recover);

    let parse_positive = |value: i32| {
        if value > 0 {
            let widened: i64 = value;
            return Ok<i64, str8>(widened);
        }
        return Err<i64, str8>("not positive");
    };
    let positive: result<i64, str8> = and_then(parsed, parse_positive);
}
```

### API

- `transform(input, operation)` maps the successful value to a new type while
  preserving the error type. The operation is called only for `Ok`.
- `transform_error(input, operation)` maps the error to a new type while
  preserving the successful value type. The operation is called only for `Err`.
- `and_then(input, operation)` invokes an operation returning another result
  for `Ok`, and propagates the original error otherwise.
- `or_else(input, recovery)` invokes a recovery operation returning another
  result for `Err`, and propagates the original value otherwise.
- `value_or(input, fallback)` returns the value for `Ok`, or the supplied value
  for `Err`. As in C++23 `expected::value_or`, the fallback expression is
  evaluated before the call.
- `value_or_else(input, fallback)` has the same result but lazily invokes its
  zero-argument closure only for `Err` (a convenience beyond the C++23 API).

These are free functions because Mlang's `result<T, E>` is a built-in generic
sum type rather than an importable class. They provide the value/error mapping
semantics of C++23 `expected::transform` and `expected::transform_error`.
