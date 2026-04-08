# Mlang Language Attributes {#language_attributes}

This document describes Rust-like attributes currently implemented in Mlang and
how to extend them.

## Supported Attributes

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
mlang test tests/test_sample.mla -L ~/.local/lib -lmlang_std
```

Run all test suites in a directory:

```sh
mlang test tests/ -L ~/.local/lib -lmlang_std
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
mlang test tests/ --filter "addition" -L ~/.local/lib -lmlang_std
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
