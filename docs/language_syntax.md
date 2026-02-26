# Mlang Language Syntax Updates {#language_syntax}

This page documents recent language syntax/features that are now supported by
the compiler.

## Type Aliases (`use type`)

Global alias:

```mla
use type Distance = f32;
use type SomeMap<K, V> = map<K, V>;
```

Block-scoped alias (shadows outer aliases inside the block only):

```mla
use type Distance = f32;

fn main() -> i32 {
    {
        use type Distance = i32;
        let grid: Distance = 42;
        println!("grid={}", grid);
    }
    let meters: Distance = 1.5;
    println!("meters={}", meters);
    return 0;
}
```

Notes:
- Aliases can be generic (`use type Name<T> = ...;`).
- Alias overlap in the same scope is rejected with a location-based diagnostic
  (`file.mla:row:column`), pointing to both current and previous declarations.
- Aliases are removed from scope when leaving the defining block.

## Numeric Primitive Names

Available primitive float types:
- `f32`
- `f64`

Language aliases:
- `float` aliases `f32`
- `double` aliases `f64`

## `if` / `else if` Syntax

Plain block form (preferred):

```mla
if x == 1 {
    println!("one");
} else if x == 2 {
    println!("two");
} else {
    println!("other");
}
```

Legacy plain-colon form remains accepted, but emits a warning when no guard is
present:

```mla
if x == 1: { println!("one"); } // warning: plain if/else-if with ':' is discouraged
```

## Guarded `if` Forms

Guard form with explicit condition + trailing guard expression:

```mla
if x >= 0: (x < 10 || x > 100) {
    println!("guard passed");
}
```

`if let`/`if var` guarded forms are supported with:
- typed initializer (`let i: i32 = expr`)
- untyped initializer (`let i = expr`)
- equality initializer (`let i == expr`)

Examples:

```mla
if let i: i32 = some(): i >= 0 && i < 10 {
    println!("i={}", i);
}

if let i == some(): ((i < 0 && i > -30) || (j > 3 && j < 5)) {
    println!("hello");
}

else if var i: i32 = some(): i >= 0 && i < 2 {
    i = 15;
}
```

Complex nested boolean guards are supported in `if` and `else if`.

## Empty Block Warning

Empty blocks are valid syntax, but emit a compiler warning:

```mla
if flag {
}
```

Diagnostic:
- `file.mla:row:column: warning: empty block`

## `while` Guard Syntax

Plain form (preferred):

```mla
while i < n {
    i += 1;
}
```

Guarded form:

```mla
while i < n: j < i && (n == m) {
    i += 1;
}
```

Notes:
- `:` is optional for plain `while cond { ... }`.
- Using `:` without a trailing guard expression is accepted but warns that it is redundant.

## Enums with Explicit Backing Type

Enums can declare explicit integer backing storage:

```mla
enum Status : i64 {
    Invalid = 1,
    Success = 2,
};
```

Compatibility and diagnostics:
- Values must fit the declared backing type.
- Compiler errors include `file.mla:row:column` locations for invalid values.
- Compatible enum values can be referenced across enums when representable in
  the target enum backing type.
- Nested enum declarations (`Outer::Inner`) are supported.

## `main` Return Type Defaulting

Both forms are supported:

```mla
fn main() {
    println!("hello");
}
```

```mla
fn main() -> i32 {
    return 0;
}
```

`fn main() { ... }` defaults to `-> i32` and returns `0` if no explicit return
is provided.

## Function Return Type Inference

Non-extern functions can omit `-> Type`, and the compiler infers the return
type from `return` expressions.

```mla
fn some() {
    return 1;         // inferred as i32
}

fn name() {
    return "alice";   // inferred as string
}
```

If no value is returned, the function is inferred as `void`.
If return forms conflict (for example both `return;` and `return value;`) or
types cannot be inferred consistently, the compiler emits an error and asks for
an explicit return type.

## Lambda + Fold Expressions

Inline typed lambda (captures outer variables, callable via bound name):

```mla
var total: i32 = 0;
var add = |x: i32| {
    total += x;
};
add(5);
```

Fold expressions over list values (C++-style shape):

```mla
let xs: list<i32> = [1, 2, 3];
let sum: i32 = (... + xs);   // left fold
let mul: i32 = (xs * ...);   // right fold

let bs: list<bool> = [true, false];
let all_true: bool = (... && bs);
let any_true: bool = (... || bs);
```

Supported fold operators:
- `+`
- `*`
- `&&`
- `||`

### Lambda/Fold Examples

Reference examples in the repository:
- `examples/lambda_fold_demo.mla`
- `examples/lambda_fold_patterns.mla`
- `examples/lambda_fold_advanced.mla`

These demonstrate:
- typed inline lambdas with captures and arguments
- nested lambdas over computed data
- left/right folds over numeric and boolean lists
- empty-list identity behavior for folds
