# Mlang Language Attributes

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

Rules:
- Parameters are not allowed.
- Return type must be `void` or `i32`.
- In test mode, `main` is not allowed (tests get a generated runner entrypoint).

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

## Adding a New Rust-like Attribute

To add a new attribute such as `#[derive(Clone)]` or `#[inline]`, update these
compiler stages:

1. Lexer: add the literal token in `src/lexer.l`.
2. Parser: add token/grammar handling in `src/parser.y`.
3. AST: store the attribute state on the relevant node in `include/ast.h` and
   `src/ast.cpp`.
4. IR/codegen: implement behavior and validation in `src/ir.cpp`.
5. Docs/examples/tests: add usage docs and a compile/runtime test.

Note:
- `#[derive(Debug)]` and `#[test]` are the only attribute forms recognized
  right now.
