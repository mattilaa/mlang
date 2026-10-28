# MLang 0.4.2

MLang 0.4.2 adds concepts and structural requirements for generic code,
user-defined literal suffixes, and nested mutable references. It also includes
compiler fixes, editor support, and expanded documentation and CI coverage.

## Language and compiler

- Added named concepts, `std::concepts` predicates, generic parameter bounds,
  and constrained overload selection. Use `where` for boolean constraints and
  `req(...) { ... }` for unevaluated checks of expressions and types.
- Added user-defined suffixes for integer, floating-point, and string literals.
  Literal operators work in namespaces and imports and can be evaluated in
  compile-time expressions.
- Added nested mutable references such as `&mut &mut T` in local bindings and
  function calls, including generic functions.
- Added bitwise AND compound assignment (`&=`).
- Fixed a compiler crash when a struct contains an invalid or unresolved field
  type; the compiler now reports the type error before creating its layout.

## Tools and documentation

- Added concept and constraint completion, hover information, semantic tokens,
  and definition navigation in the language servers.
- Added runnable examples and reference material for constraints, literal
  suffixes, nested references, and the debugger.
- Added AddressSanitizer builds and tests on Linux and macOS CI.

## Migration notes

- Replace the former `requires` spelling with `where` for boolean constraints
  and nested predicates, or `req` for structural checks. The old spelling now
  produces `MLANG-E1020`.
- Nested mutable references cannot yet rebind borrowed reference slots through
  a function parameter; mutate the value reached by the reference instead.

# MLang 0.4.1

MLang 0.4.1 is a maintenance release that corrects compiler version reporting
and updates the release instructions and metadata to match the 0.4.1 release.

## Fixes

- Corrected fallback version strings used when the compiler and compiler API
  are built outside the standard CMake configuration.
- Updated release examples and artifact names to use `v0.4.1`.

# MLang 0.4.0

MLang 0.4.0 is a major language release centered on safer borrowing and
first-class callable values. The compiler now tracks how references and
pointer-like values flow through expressions, control flow, calls, closures,
and aggregate data, and reports conflicts before the program runs.

## Language and compiler

- Added local shared and exclusive borrow checking with `&value` and
  `&mut value`. Conflicting reads, writes, moves, and overlapping borrows are
  diagnosed; borrows normally end after their last use.
- Made borrow analysis field-sensitive and flow-sensitive. Disjoint struct
  fields and known constant indices can be borrowed independently, while
  possible aliases are checked conservatively across branches, `match`, loops,
  and early exits.
- Propagated borrow provenance through pointer aliases, helper and method
  calls, returned pointers, closures, and aggregates such as structs, tuples,
  lists, maps, and fixed arrays. This prevents a borrow from being hidden in a
  container or lost at a control-flow join.
- Added checks for partial moves and moved values, including cleanup-managed
  values and values stored in aggregate fields or indexed elements.
- Added callable function types with Rust-like syntax, such as
  `fn(i32) -> i32`, and callable type aliases. Compatible named functions and
  closures can be passed as callbacks, stored in typed locals, reassigned,
  returned when safe, and invoked through their callable value.
- Expanded closure support with typed and contextually inferred parameters and
  results, generic callbacks, runtime capture environments, and borrowed
  captures. The compiler checks that a borrowed closure cannot escape its
  captures when it can prove the callee's behavior.
- Added `std::copy(value)` for explicit value-copy requests where copying is
  supported. List copies allocate separate list storage and copy elements;
  this is shallow for pointer-like fields and does not introduce a general
  `Clone` mechanism.
- Updated `std::sync` output-pointer calls to use explicit mutable borrows and
  unsafe blocks where the C interface writes through pointers.

## Safety and current limitations

- Dereferencing raw pointers requires `unsafe`. Unsafe code does not override
  borrow facts the compiler already knows: writes through a pointer known to be
  a shared borrow are still rejected. Unknown raw pointers remain the unsafe
  caller's responsibility and are runtime-checked for null where applicable.
- Passing a closure that borrows local values across a function boundary is
  accepted only when the compiler proves that the callback does not escape.
  The proof is intentionally conservative; unrecognized control-flow or call
  patterns are rejected rather than assumed safe.
- Returning a locally borrowed closure, general closure copying, and explicit
  owned/move captures are not supported. Non-borrowing closures can be returned
  when their capture environment is proven safe.
- Borrow summaries for opaque or external pointer-taking functions are
  conservative because their implementation is unavailable to the compiler.
  Passing a local borrow to such a function requires an explicit `unsafe`
  boundary.

## Migration notes

- Use `&mut value` when a pointer is intended to mutate a value. Raw-pointer
  dereferences and calls that cross an opaque pointer boundary may need an
  `unsafe` block.
- Callable values can be declared with a signature such as
  `let map: fn(i32) -> i32 = |value| { return value + 1; };`. Overloaded named
  functions need an explicit callable type when the intended overload cannot
  be inferred.
- Use `Vec::new()` for an empty `Vec<T>`; a generic free-function call such as
  `new()` does not currently infer its type arguments at the call site.
