# std::multiarray

Module file: `stdlib/std/multiarray.mla`

Documentation/navigation module for the compiler-provided immutable
`multiarray<T, D1, ..., DN>` type.

## Import and declaration

```mla
mod std::multiarray;

let matrix: multiarray<i32, 2, 3> = {
    {1, 2, 3},
    {4, 5, 6}
};
```

- One or more dimensions are supported.
- Every `D1 ... DN` value is a compile-time capacity.
- Nested initializer sizes are checked against their corresponding extents.
- Elements are immutable. Declaring the binding with `var` does not permit
  indexed writes; use `mutmultiarray` for that.

## Indexing and bounds

Use one chained index for each dimension:

```mla
let value: i32 = matrix[1][2]; // 6
```

Constant out-of-bounds indexes are compile-time errors. Dynamic indexes emit a
runtime bounds guard and abort before an invalid load.

## See also

- [`std::mutmultiarray`](std_mutmultiarray.md) for mutable elements and
  recoverable `get(...)` access.
- [`std::array`](std_array.md) for a one-dimensional fixed-capacity sequence.
- `examples/multiarray.mla` for a runnable example.
