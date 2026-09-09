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
- `transpose()` — swap rows and columns of any numeric 2D matrix. An `R × C`
  input produces an immutable `C × R` matrix.
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

## Square floating-point operations

Square `f32` and `f64` matrices provide these additional methods:

- `determinant()` returns the scalar determinant. A zero determinant indicates
  that the matrix is singular and has no inverse.
- `inverse()` returns a new immutable matrix. It aborts with a diagnostic when
  the matrix is singular.
- `eigenvalues()` returns the real eigenvalues in ascending order.
- `eigenvectors()` returns a matrix whose columns are normalized eigenvectors;
  column `i` corresponds to eigenvalue `i` from `eigenvalues()`.

The eigenvalue methods use a Jacobi solver and therefore accept real symmetric
matrices only. They abort with a diagnostic for a non-symmetric input or if the
solver does not converge. For repeated eigenvalues, the particular orthonormal
basis returned for the repeated eigenspace is not guaranteed.

```rust
mod std::matrix;

let matrix: multiarray<f64, 2, 2> = {{4.0, 7.0}, {2.0, 6.0}};
let determinant: f64 = matrix.determinant(); // 10.0
let inverse: multiarray<f64, 2, 2> = matrix.inverse();

let symmetric: multiarray<f64, 2, 2> = {{2.0, 1.0}, {1.0, 2.0}};
let values: multiarray<f64, 2> = symmetric.eigenvalues(); // {1.0, 3.0}
let vectors: multiarray<f64, 2, 2> = symmetric.eigenvectors();
// symmetric.matmul(vectors) scales each vector column by its eigenvalue.
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

See `examples/matrix_operations.mla` for a runnable visual walkthrough that
prints the original multiarrays and every matrix result as labeled 2D boxes.
When run in a terminal, it opens an interactive operation selector: press the
shortcut shown in its black-on-white command bar to redraw the result, or `Q`
to quit. The matrix borders use a gray ANSI color. Pass `--batch` to print all
operations sequentially instead.
After compiling it, pass `--size=ROWSxCOLS`, such as `--size=6x7`, to run the
rectangular operations on a larger logical matrix. Each dimension may be from
1 through 10; the example uses fully initialized fixed `10x10` storage because
`multiarray` dimensions are compile-time types.
