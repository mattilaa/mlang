# std::mutmultiarray

Module file: `stdlib/std/mutmultiarray.mla`

Documentation/navigation module for the compiler-provided mutable
`mutmultiarray<T, D1, ..., DN>` type.

## Import and declaration

```mla
mod std::mutmultiarray;

var matrix: mutmultiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
matrix[0][1] = 42;
matrix[1][0] += 10;
```

- Every dimension has a compile-time-fixed capacity.
- Mutation requires a `var` binding. A `let` binding remains immutable.
- Indexed and compound assignments are supported.

## Bounds behavior

Direct `[]` access is checked at every dimension:

- A constant invalid index is a compile-time error.
- A dynamic invalid index aborts at runtime before reading or writing.

Use `get(i1, ..., iN)` when an invalid dynamic index should be recoverable.
It accepts exactly one index per dimension and returns `option<T>`:

```mla
var row: i32 = 1;
let found: bool = matrix.get(row, 0).is_some();
let missing: bool = matrix.get(2, 0).is_none();
let value: i32 = matrix.get(row, 0).unwrap();
```

- `is_some()` is true for an in-bounds element.
- `is_none()` is true if any index is out of bounds.
- `unwrap()` returns the element when present. On `None`, it reports the source
  file and line and aborts.

## See also

- [`std::multiarray`](std_multiarray.md) for immutable multidimensional data.
- [`std::array`](std_array.md) for a one-dimensional fixed-capacity sequence.
- `examples/multiarray.mla` for a runnable example.
