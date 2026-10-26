# std::concepts

Compiler-supported named concepts for builtin types. Import them with:

```mla
mod std::concepts;
use std::concepts::*;

generic<T> where arithmetic<T>
fn twice(value: T) -> T { return value + value; }
```

| Concept | Satisfied by |
| --- | --- |
| `same_as<T, U>` | The same resolved type, including pointer/reference structure and mutability |
| `integral<T>` | `bool`, `bit`, and the signed/unsigned 8-, 16-, 32-, and 64-bit integers |
| `signed_integral<T>` | `i8`, `i16`, `i32`, `i64` |
| `unsigned_integral<T>` | `u8`, `u16`, `u32`, `u64`, `bool`, `bit` |
| `floating_point<T>` | `f32`, `f64` |
| `arithmetic<T>` | Integral or floating-point types |

Concepts produce compile-time booleans. A query such as
`std::concepts::same_as<&i32, &mut i32>` is false. Aliases of the same type compare
equal. Nominal enums and structs are not classified as builtin arithmetic types.

Use a unary concept in a bound (`generic<T: integral>`) or compose concepts in a
`where` clause. `same_as` takes two type arguments and belongs in a clause or
boolean expression. Constraint failure rejects an overload before instantiating
its body; a stronger named constraint wins over a weaker matching overload.

The declarations live in `stdlib/std/concepts.mla`. The language guide documents
concept definitions, module visibility, short-circuiting, and overload ordering;
`examples/concepts_demo.mla` demonstrates runtime calls and compile-time queries.

Concepts also constrain structural expressions. For example,
`concept Addable<T> = req(a: T, b: T) { { a + b } -> same_as<T>; };`
tests the operation and its result type without evaluating it. See
`examples/structural_concepts_demo.mla` for a runnable example using simple,
type, compound, and nested requirements.

See the [concepts guide](../concepts.md) for a complete example, requirement forms,
overload ordering, and current compatibility limits.

Editor tooling indexes concept declarations for definition navigation and hover.
`gd` on an imported concept leads to its declaration in `std/concepts.mla`;
`gd` on keywords such as `where` and `req` leads to their builtin reference in
`types.mla`. The builtin reference also describes every standard concept for
editor discovery. Specific import aliases retain the original definition target.
