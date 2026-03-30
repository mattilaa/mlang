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

Available primitive floating-point types:
- `f32`
- `f64`

## Type Name Property (`.name`)

Values expose a read-only synthetic `.name` property for logging static type
names:

```mla
let i: i32 = 12;
println!("{}", i.name);  // i32
```

For collection values, the returned name includes inner types when available:
- `list<i32>`
- `map<str8, i32>`

If a struct defines a real field named `name`, normal field access is used
instead of the synthetic type-name property.

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

## `switch` / `case`

MLang supports block-style `switch` statements with direct `case value: { ... }`
syntax.

Example:

```mla
enum SwitchColor {
    Red,
    Green,
    Blue
};

fn main() -> i32 {
    let color: SwitchColor = SwitchColor::Green;
    var text: str8 = "unset";

    switch color {
        case SwitchColor::Red: {
            text = "red";
        }
        case SwitchColor::Green: {
            text = "green";
        }
        default: {
            text = "other";
        }
    }

    println!("{}", text);
    return 0;
}
```

Supported practical case-value forms include:
- integer and floating-point literals
- `str8` string literals and variables
- `str16` variables
- booleans
- enum values such as `SwitchColor::Green`
- identifiers, function calls, casts, and comparison-capable expressions

Notes:
- `switch` evaluates its subject expression once.
- `default` is optional.
- `case` values use the direct syntax `case value: { ... }`.
- The current implementation lowers `switch` into an equivalent `if` / `else if`
  chain, so matching relies on the existing `==` support for the compared type.

## Exceptions: `throw` and `try/catch`

MLang supports stack-unwinding exceptions with an explicit payload type from
`std::exceptions`.

## Functional Programming Subset

MLang already supports a practical Haskell/OCaml-style subset inside normal
MLang code:

- immutable `let` bindings for pure intermediate values
- pipe operator `|>` for left-to-right function composition
- closures, including typed parameter closures such as `|x: i32| { ... }`
- algebraic-data-type style `Option<T>` and `Result<T, E>`
- `match` expressions over enums, `Option`, `Result`, and literals
- tuple types and tuple literals
- fold expressions over lists

### Pipe Operator: `|>` {#pipe_operator}

MLang supports a functional pipe operator that forwards the value on the left
as the first argument of the function on the right.

Examples:

```mla
fn add1(x: i32) -> i32 { return x + 1; }
fn mul2(x: i32) -> i32 { return x * 2; }
fn add(x: i32, y: i32) -> i32 { return x + y; }

fn main() -> i32 {
    let value: i32 = 5 |> add1() |> mul2() |> add(3);
    println!("{}", value); // 15
    return 0;
}
```

Current supported target forms:
- `value |> f`
- `value |> f()`
- `value |> mod::f(a, b)`

Lowering rules:
- `x |> f` becomes `f(x)`
- `x |> f()` becomes `f(x)`
- `x |> f(a, b)` becomes `f(x, a, b)`

The operator is left-associative, so chained pipelines evaluate left to right.

This is intended for ML-style left-to-right composition inside ordinary MLang
code without introducing a separate runtime abstraction.

Example:

```mla
fn choose(flag: bool) -> Option<i32> {
    if flag {
        return Some<i32>(42);
    }
    return None<i32>();
}

fn main() -> i32 {
    let values: list<i32> = [1, 2, 3, 4];
    let total: i32 = (... + values);

    let maybe_value: Option<i32> = choose(true);
    let answer: i32 = match maybe_value {
        Some(v) => v,
        None => 0
    };

    var boosted: i32 = total;
    let add_answer = |x: i32| {
        boosted += x;
    };
    add_answer(answer);

    println!("answer={} boosted={}", answer, boosted);
    return 0;
}
```

See also:
- `examples/pipe_operator_demo.mla`
- `examples/functional_option_result_demo.mla`
- `examples/functional_closure_fold_demo.mla`
- `examples/lambda_fold_patterns.mla`
- `examples/lambda_fold_advanced.mla`

Example:

```mla
mod std::exceptions;
use std::exceptions::*;

#[inline(never)]
fn parse_number(text: str8) -> i32 {
    if text == "42" {
        return 42;
    }
    throw with_line("ParseError", "expected 42", 12);
}

fn main() -> i32 {
    try {
        let value: i32 = parse_number("x");
        println!("value={}", value);
    } catch e: Exception {
        println!("caught {} at {}: {}", e.type_name, e.source_line, e.message);
    }
    return 0;
}
```

Notes:
- `throw expr;` transfers control to the nearest enclosing `catch`.
- `catch e: Exception { ... }` binds the thrown exception payload for the
  handler body.
- Exceptions propagate across nested function calls until a matching `catch`
  is found.
- Scope-owned values are cleaned up during exception unwinding before control
  enters the handler.
- Uncaught exceptions terminate the program after printing the exception type,
  message, and source line when available.

## Inline Assembly: `asm`

MLang supports expression-style inline assembly lowered directly to LLVM inline
asm.

Supported source forms:
- `asm(T, "template", operands...)`
- `asm volatile(T, "template", operands...)`
- `asm x86(T, "template", operands...)`
- `asm x64(T, "template", operands...)`
- `asm aarch64(T, "template", operands...)`
- `asm volatile x86(...)`, `asm volatile x64(...)`,
  `asm volatile aarch64(...)`

Examples:

```mla
fn main() -> i32 {
    let value: i64 = 41;
    let one: i64 = 1;
    let sum: i64 = asm x64(i64, "addq $2, $0", value, one);
    asm volatile x64(void, "");
    if sum == 42 {
        return 0;
    }
    return 1;
}
```

```mla
fn main() -> i32 {
    let base: i64 = 40;
    let delta: i64 = 2;
    let sum: i64 = asm aarch64(i64, "add $0, $1, $2", base, delta);
    asm volatile aarch64(void, "yield");
    if sum == 42 {
        return 0;
    }
    return 1;
}
```

Constraints:
- The result type is explicit in source; use `void` for side-effect-only asm.
- Inline asm currently supports only integer, pointer, or `void` result types.
- Operands currently support only integer and pointer values.
- Non-`void` inline asm must have at least one operand.
- For non-`void` inline asm, the first operand must have the same type as the
  declared result type. It is used as the tied input/output register.
- `asm volatile(...)` is required when side effects must not be optimized away.
- Arch-qualified forms require a matching compilation target selected with
  `--target-arch x86`, `--target-arch x64`, or `--target-arch aarch64`.
- The compiler reports an error if the inline asm architecture qualifier does
  not match the active target architecture.

Reference examples in the repository:
- `examples/inline_asm_x64_demo.mla`
- `examples/inline_asm_aarch64_demo.mla`
- `examples/inline_asm_x64_hello_demo.mla`
- `examples/inline_asm_aarch64_hello_demo.mla`
- `examples/inline_asm_x64_data_hello_demo.mla`
- `examples/inline_asm_aarch64_data_hello_demo.mla`

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
    return "alice";   // inferred as str8
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

## `bit` and `sizeof`

MLang provides a builtin `bit` type for logical `0` / `1` values:

```mla
var state: bit = 0;
state = 1;
state = bit(0);
```

`bit` is useful for flags stored in structs and other low-level state. For
readable aliases you can wrap the two values in helpers such as
`std::bits::ON()` and `std::bits::OFF()`.

The builtin `sizeof(...)` returns the ABI byte size as `i64`:

```mla
println!("bit={} bool={} header={}",
         sizeof(bit), sizeof(bool), sizeof(list<bool>));
```

Both forms are supported:
- `sizeof(Type)`
- `sizeof(expr)`

When the size can be resolved at compile time, `sizeof(...)` can also be used
inside `static_assert!`:

```mla
let view: Span<i32> = [1, 2, 3];
static_assert!(sizeof(view) == sizeof(list<i32>));
```

Important distinction:
- `list<bool>` is a normal list container with ordinary element storage
- `std::bitset::BitSet` stores values densely at one bit per entry
