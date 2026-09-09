# std::matrix

Module file: `stdlib/std/matrix.mla`

`std::matrix` documents numeric operations on fixed-size `multiarray` and
`mutmultiarray` values. Operands must be fully initialized; a runtime shape
check aborts before reading incomplete storage.

## Immutable results

These methods accept integer or floating-point element types and return a new,
immutable `multiarray`:

- `add(other)` and `subtract(other)` — element-wise arithmetic on equal shapes.
- `hadamard(other)` — element-wise multiplication on equal shapes.
- `offset(value)` — add a scalar to every element.
- `scale(value)` — multiply every element by a scalar.
- `matmul(other)` / `multiply(other)` — conventional multiplication of
  compatible 2D matrices. For `A<R, K>` and `B<K, C>`, the result is `R × C`.
- `sum()` — reduce every element to one scalar.

Element-wise operations and `sum` support any number of dimensions. Matrix
multiplication requires exactly two dimensions.

```rust
mod std::matrix;

let a: multiarray<i32, 2, 3> = {{1, 2, 3}, {4, 5, 6}};
let b: multiarray<i32, 3, 2> = {{7, 8}, {9, 10}, {11, 12}};
let product: multiarray<i32, 2, 2> = a.matmul(b);
let shifted: multiarray<i32, 2, 3> = a.offset(10);
println!("{} {}", product[0][0], shifted.sum()); // 58 81
```

## Mutable in-place operations

A `var mutmultiarray` supports `add_assign`, `subtract_assign`,
`hadamard_assign`, `offset_assign`, and `scale_assign`. Equal-shape operations
accept either immutable or mutable multiarrays as the right operand.

```rust
var values: mutmultiarray<i32, 2, 2> = {{1, 2}, {3, 4}};
values.offset_assign(1);
values.scale_assign(2); // {{4, 6}, {8, 10}}
```

See `examples/matrix_operations.mla` for a runnable example.
