# std::optional

Module file: `stdlib/std/optional.mla`

Monadic and fallback operations for Mlang's built-in `option<T>`, following
C++23 `std::optional`. These are free functions because `option<T>` is a
built-in sum type.

```mla
mod std::optional;
use std::optional::transform;
use std::optional::and_then;
use std::optional::value_or;

fn example() {
    let input: option<i32> = Some<i32>(21);
    let double = |value: i32| { return value * 2; };
    let doubled: option<i32> = transform(input, double);
    let zero: i32 = 0;
    let value: i32 = value_or(doubled, zero);
}
```

### API

- `transform(input, operation)` maps `Some(value)` and preserves `None`; the
  output option's element type is inferred from the operation.
- `and_then(input, operation)` chains an operation returning another option;
  `None` skips the operation.
- `or_else(input, fallback)` returns the original `Some`, or lazily invokes a
  zero-argument closure to produce an option for `None`.
- `value_or(input, fallback)` extracts a `Some` value or returns the supplied
  fallback. The fallback expression is evaluated before the call, as in C++.
- `value_or_else(input, fallback)` lazily computes a plain fallback value for
  `None`; this convenience complements C++23's API.
