# Mlang Language Attributes {#language_attributes}

This document describes Rust-like attributes currently implemented in Mlang and
how to extend them.

## Supported Attributes

| Attribute | Target | Purpose |
|---|---|---|
| `#[derive(Debug)]` | `struct` definitions | Enables debug formatting for structs. |
| `#[derive(Json)]` | `struct` definitions | Synthesizes JSON serialization/deserialization helpers for supported structs. |
| `#[test]` | `fn` definitions | Marks a function as a test case. |

## `#[derive(Debug)]`

Applies to `struct` definitions (including `pub struct` and generic structs).

What it enables:
- Debug formatting with `{:?}` and `{:#?}` in `format!`, `println!`, etc.
- Passing structs directly to `println!(value)` style calls.

Example:

```mla
#[derive(Debug)]
struct Point {
    var x: i32;
    var y: i32;
};

fn main() -> i32 {
    let origin: Point = Point { x: 0, y: 0 };
    println!("{:?}", origin);
    println!(origin);
    return 0;
}
```

## `#[derive(Json)]`

Applies to `struct` definitions, including derived structs and generic
structs.

What it enables:
- `value.to_json() -> str8`
- `StructName::from_json(text) -> Result<StructName, str8>`
- JSON output that includes inherited base fields directly in the object
- A sibling `@property` metadata object for fields declared with
  `@property(...)`

Current `from_json(...)` field support:
- `bool`
- signed and unsigned integer primitives
- `f32` and `f64`
- `str8`
- nested structs that also derive `Json`

Example:

```mla
#[derive(Json)]
struct Base {
    @property(hidden) var secret: i32;
    var x: i32;
};

#[derive(Json)]
struct Leaf : Base {
    var name: str8;
};

fn main() -> i32 {
    var leaf: Leaf {};
    leaf.setSecret(7);
    leaf.x = 3;
    leaf.name = String::from("ok");

    let text: str8 = leaf.to_json();
    let parsed: Result<Leaf, str8> = Leaf::from_json(text);
    return parsed.is_ok() ? 0 : 1;
}
```

Generated JSON shape:

```json
{
  "type": "Leaf",
  "secret": 7,
  "x": 3,
  "name": "ok",
  "@property": {
    "secret": {
      "hidden": true,
      "protected": false,
      "atomic": false,
      "mutex": false,
      "recursive": false
    }
  }
}
```

`from_json(...)` reads field values from the normal object keys. The
`@property` subtree is emitted for tooling and inspection and is not required
for deserialization.

## `#[test]`

Applies to function definitions and marks them as test functions for:
- `mlang test`
- `mlang run tests`
- `mlang bench` (benchmark runner for `#[test]` functions)

Rules:
- Parameters are not allowed.
- Return type must be `void` or `i32`.
- In test mode, `main` is not allowed (tests get a generated runner entrypoint).
- In benchmark mode, the same `#[test]` functions are executed in timed loops.

### Running tests

Run all tests in a single file:

```sh
mlang test tests/test_sample.mla -L ~/.local/lib/mlang -lmlang_std
```

Run all test suites in a directory:

```sh
mlang test tests/ -L ~/.local/lib/mlang -lmlang_std
```

### Filtering individual tests

Use `--filter <name>` to run only tests whose display name (`suite.case`) or
raw function name contains the given substring:

```sh
# Run only the "addition" test
mlang test tests/test_sample.mla --filter "addition"

# Filter by the raw function name
mlang test tests/test_sample.mla --filter "test_result_ok"

# Filter by the full display name (suite.case)
mlang test tests/test_sample.mla --filter "test_sample.result ok"
```

The filter is also forwarded in directory mode, so it works across all suites:

```sh
mlang test tests/ --filter "addition" -L ~/.local/lib/mlang -lmlang_std
```

### Test naming

Each test is reported as `suite.case`:
- **Suite name**: derived from the source filename stem (e.g.
  `std_math_tests.mla` → `std_math_tests`). Path separators, colons, hyphens,
  and spaces are replaced with dots.
- **Case name**: derived from the function name. A leading `test_` prefix is
  stripped and underscores are replaced with spaces (e.g. `test_result_ok` →
  `result ok`).

### Other test flags

- `--no-run` compile tests but do not execute them.
- `--no-tests` (outside test mode) skip compiling `#[test]` functions entirely.

### Benchmark runner flags

- `--bench-iters N` measured iterations per benchmark (default: 100 000).
- `--bench-warmup N` warmup iterations before timing (default: 10 000).

Example:

```mla
#[test]
fn test_addition() -> i32 {
    let x: i32 = 2 + 2;
    if x == 4: {
        return 0;
    }
    return 1;
}
```

Benchmark example:

```mla
mod std::bench;

#[test]
fn bench_counter() -> i32 {
    let v: i64 = 42;
    do_not_optimize_i64(v);
    clobber_memory();
    return 0;
}
```

See:
- [`tests/test_sample.mla`](../../../tests/test_sample.mla) — basic unit test example
- [`tests/bench_stdlib.mla`](../../../tests/bench_stdlib.mla) — benchmark example
- [`tests/bench_simd.mla`](../../../tests/bench_simd.mla) — scalar vs `std::simd` benchmark pairs
- [`tests/fixture_tests.mla`](../../../tests/fixture_tests.mla) — test fixture example

## `#[fixture]`

Applies to inherent `impl` blocks. Marks the impl as a *test fixture*: every
`#[test]` method inside runs against a **fresh, stack-allocated, zero-initialized
instance** of the struct, mirroring GoogleTest's `TEST_F` semantics.

Hooks (both optional, looked up by name on the struct):
- `setup(self: &mut Self) -> void` — runs before each `#[test]` method.
- `teardown(self: &mut Self) -> void` — runs after each `#[test]` method.

Rules for `#[test]` methods inside a `#[fixture] impl`:
- Must take exactly `self: &mut Self` (no extra parameters).
- Must return `void` or `i32`.
- May not be `static`; trait impls (`impl Trait for Struct`) are not eligible.

Example:

```mla
mod std::testing;
use std::testing::*;

struct Counter {
    var value: i32;
};

#[fixture]
impl Counter {
    fn setup(self: &mut Self) -> void {
        self.value = 100;
    }

    fn teardown(self: &mut Self) -> void {
        // close handles, free buffers, etc.
    }

    #[test]
    fn test_setup_runs(self: &mut Self) -> void {
        expect_eq_i32(100, self.value);
    }

    #[test]
    fn test_isolation(self: &mut Self) -> void {
        // Fresh instance — value is 100 again, not whatever the previous
        // test left behind.
        expect_eq_i32(100, self.value);
        self.value = 999;
    }
}
```

Test reports use the form `suite.Struct_<case>`, e.g.
`fixture_tests.Counter_setup runs`.

## `#[inline]`

Applies to function definitions. Suggests to the compiler that the function
should be inlined at call sites (maps to LLVM `InlineHint`).

## `#[inline(always)]`

Applies to function definitions. Strongly suggests that the function must
always be inlined (maps to LLVM `AlwaysInline`).

## `#[inline(never)]`

Applies to function definitions. Strongly suggests that the function must
never be inlined (maps to LLVM `NoInline`).

Example:

```mla
#[inline]
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

#[inline(always)]
fn fast_mul(a: i32, b: i32) -> i32 {
    return a * b;
}

#[inline(never)]
fn debug_only(x: i32) -> i32 {
    println!("{}", x);
    return x;
}
```

## `#[x86-64]`

Applies to function definitions. The annotated function is included only when
compiling/running on an x86-64 target/host.

Typical use case:
- arch-specific inline asm helpers

Example:

```mla
#[x86-64]
fn arch_sum(lhs: i64, rhs: i64) -> i64 {
    return asm x64(i64, "movq $1, $0\naddq $2, $0", lhs, rhs);
}
```

## `#[aarch64]`

Applies to function definitions. The annotated function is included only when
compiling/running on an AArch64 target/host.

Example:

```mla
#[aarch64]
fn arch_sum(lhs: i64, rhs: i64) -> i64 {
    return asm aarch64(i64, "add $0, $1, $2", lhs, rhs);
}
```

These attributes are intended for cases where ordinary `if` branches are not
enough because non-matching inline asm would still be validated by the
compiler. See:
- `examples/platform_inline_asm_demo.mla`
- `tests/arch_attr_inline_asm_tests.mla`
- `tests/inline_asm_target_arch_tests.mla`

## Conditional Regions

For larger platform/arch-specific code snippets, MLang also supports raw
conditional regions that are removed before parsing, closer to `#ifdef` in C/C++.

Supported region tags:
- `[windows] ... [/windows]`
- `[posix] ... [/posix]`
- `[linux] ... [/linux]`
- `[macos] ... [/macos]`
- `[x86-64] ... [/x86-64]`
- `[aarch64] ... [/aarch64]`

Example:

```mla
[windows]
fn platform_name() -> str8 {
    return "windows";
}
[/windows]

[posix]
fn platform_name() -> str8 {
    return "posix";
}
[/posix]
```

Unlike ordinary `if` statements, non-matching regions are filtered out before
the parser sees them, so duplicate definitions and target-specific code such as
inline asm can live in separate regions safely.

See:
- `examples/platform_region_demo.mla`
- `tests/std_platform_tests.mla`

## `@property(...)`

Applies to struct fields and synthesizes accessor methods for the field:
- `get<Field>() -> T`
- `set<Field>(value: T) -> void` for mutable `var` fields

Basic example:

```mla
struct Device {
    @property var value: i32;
};

fn main() -> i32 {
    var d: Device = Device {};
    d.setValue(7);
    return d.getValue();
}
```

Supported options:
- `@property(atomic)`:
  Generates atomic load/store accessors for integer and `bool` fields.
- `@property(mutex)`:
  Protects accessor bodies with a synthesized mutex.
- `@property(mutex, recursive)`:
  Uses a recursive mutex. `recursive` requires `mutex`.
- `@property(hidden)`:
  Hides the backing field from direct access outside the declaring struct's own
  methods. External code must use synthesized accessors or explicit methods on
  the struct.
- `@property(protected)`:
  Allows direct field access only inside the declaring struct and structs in
  its full derive chain. If `Derived : Base` and `Leaf : Derived`, then a
  protected property declared in `Base` is directly accessible from `Base`,
  `Derived`, and `Leaf`, but not from unrelated code.

Example with `hidden`:

```mla
struct SecretBox {
    @property(hidden) var code: i32;

    fn reveal(self: SecretBox) -> i32 {
        return self.code;
    }
};

fn main() -> i32 {
    var box: SecretBox = SecretBox {};
    box.setCode(42);
    return box.reveal();
}
```

Example with `protected` across a derive chain:

```mla
struct Base {
    @property(protected) var value: i32;
};

struct Mid : Base {
    var tag: i32;
};

struct Leaf : Mid {
    fn read(self: Leaf) -> i32 {
        return self.value;
    }
};
```

Constraints:
- `recursive` requires `mutex`.
- `atomic` cannot be combined with `mutex`.
- `hidden` and `protected` are mutually exclusive.

## Adding a New Rust-like Attribute

To add a new attribute such as `#[derive(Clone)]`, update these compiler stages:

1. Lexer: add the literal token in `src/lexer.l`.
2. Parser: add token/grammar handling in `src/parser.y`.
3. AST: store the attribute state on the relevant node in `include/ast.h` and
   `src/ast.cpp`.
4. IR/codegen: implement behavior and validation in `src/ir.cpp`.
5. Docs/examples/tests: add usage docs and a compile/runtime test.

Note:
- `#[derive(Debug)]`, `#[test]`, `#[inline]`, `#[inline(always)]`, and
  `#[inline(never)]`, `#[x86-64]`, and `#[aarch64]` are the attribute forms
  currently recognized.
