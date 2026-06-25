# Mlang Language Syntax Updates {#language_syntax}

This page documents recent language syntax/features that are now supported by
the compiler.

## Namespace Blocks

MLang supports C++17-style namespace blocks for grouping declarations under a
qualified path:

```mla
namespace geometry::units {
    alias Distance = f32;

    struct Reading {
        let value: Distance = 0.0f;
    };

    fn average(a: Distance, b: Distance) -> Distance {
        return (a + b) / 2.0f;
    }
}

fn main() -> i32 {
    let d: geometry::units::Distance = 10.0f;
    let r: geometry::units::Reading = geometry::units::Reading {
        value: geometry::units::average(d, 14.0f)
    };
    return r.value > 0.0f ? 0 : 1;
}
```

Declarations inside the block are available by their fully qualified names, such
as `geometry::units::Reading`. Inside the namespace block, local declarations can
refer to each other by short name, so the `Reading.value` field above can use
`Distance` directly. Namespace blocks currently flatten to qualified top-level
declarations; they are for declaration ownership and naming, not separate files.

Namespace aliases shorten long qualified paths:

```mla
namespace gu = geometry::units;
alias ga = geometry::units;

fn main() -> i32 {
    let d: gu::Distance = 10.0f;
    let r: gu::Reading = gu::Reading {
        value: gu::average(d, 14.0f)
    };
    let s: ga::Reading = ga::Reading {
        value: ga::average(2.0f, 4.0f)
    };

    {
        namespace local_units = geometry::units;
        let local: local_units::Reading = local_units::Reading {
            value: local_units::average(2.0f, 4.0f)
        };
    }

    return 0;
}
```

`alias alias_name = some::qualified::path;` is the preferred short spelling for
namespace aliases. The explicit `namespace alias_name = some::qualified::path;`
form is also accepted. Namespace aliases can be declared at top level or inside
a block/function. They affect qualified names parsed after the declaration, so
`ga::Reading` resolves as `geometry::units::Reading`.

## Trait Objects (`dyn Trait`) {#trait_objects_dyn}

MLang supports explicit trait-object types for runtime dispatch at function
boundaries:

```mla
trait Summary {
    fn score(self: Self) -> i32;
}

pub struct Post {
    var score_value: i32;
};

impl Summary for Post {
    fn score(self: Post) -> i32 {
        return self.score_value;
    }
}
```

Concrete values can be passed to functions expecting `dyn Trait`:

```mla
pub fn show_score(item: dyn Summary) -> i32 {
    return item.score();
}
```

Concrete values can also be returned through a dyn return type. The compiler
creates the trait-object pair and stores a durable copy of the concrete value
behind it:

```mla
pub fn make_summary(post: Post) -> dyn Summary {
    return post;
}
```

Existing dyn values can be passed through wrappers without losing dispatch:

```mla
pub fn pass_summary(item: dyn Summary) -> dyn Summary {
    return item;
}
```

Callers may use a fully-qualified trait path when crossing module boundaries:

```mla
mod lib::summary;
use lib::summary::Post;

fn main() -> i32 {
    let post: Post = Post { score_value: 7 };
    let item: dyn lib::summary::Summary = lib::summary::make_summary(post);
    let item2: dyn lib::summary::Summary = lib::summary::pass_summary(item);
    return lib::summary::show_score(item2) == 7 ? 0 : 1;
}
```

Use `dyn Trait` when the call boundary should use runtime dispatch. Use
generic bounds such as `T: Trait` when the type should remain statically
known and monomorphized.

## Type Aliases (`alias` / `use type`)

Global alias:

```mla
alias Distance = f32;
alias SomeMap<K, V> = map<K, V>;
```

Block-scoped alias (shadows outer aliases inside the block only):

```mla
alias Distance = f32;

fn main() -> i32 {
    {
        alias Distance = i32;
        let grid: Distance = 42;
        println!("grid={}", grid);
    }
    let meters: Distance = 1.5;
    println!("meters={}", meters);
    return 0;
}
```

Notes:
- Aliases can be generic (`alias Name<T> = ...;`).
- `use type Name = Type;` remains valid and equivalent. Prefer `alias` for
  ordinary type alias declarations; keep `use` for imports.
- Alias overlap in the same scope is rejected with a location-based diagnostic
  (`file.mla:row:column`), pointing to both current and previous declarations.
- Aliases are removed from scope when leaving the defining block.

## Numeric Primitive Names

Available primitive floating-point types:
- `f32`
- `f64`

String type spellings:
- `str8` and `utf8` are UTF-8 string types.
- `str16` and `utf16` are UTF-16 string types.
- `string` remains accepted as a UTF-8 spelling.

String literals support common escapes plus byte-valued hexadecimal escapes:

```mla
let plain: utf8 = "hello\n";
let colored: utf8 = "\x1b[38;2;164;255;82mhello\x1b[0m";
```

Use `\xNN` for terminal escape bytes such as `\x1b` (`ESC`).

## Platform Macros

MLang supports builtin platform macros for multiplatform source selection:

- `windows!()`
- `posix!()`
- `linux!()`
- `macos!()`
- `x64!()`
- `aarch64!()`

These evaluate to compile-time boolean values, so they work in normal control
flow and in `static_assert!`.

Example:

```mla
static_assert!(windows!() || posix!());

if windows!() {
    println!("windows code path");
} else if posix!() {
    println!("posix code path");
}
```

## Architecture-Gated Functions

MLang supports compile-time architecture-gated function definitions with:

- `#[x86-64]`
- `#[aarch64]`

Only the matching function definition is included in the compiled program.
This is useful for inline asm implementations that would otherwise fail
validation on the wrong target.

Example:

```mla
#[aarch64]
fn arch_sum(lhs: i64, rhs: i64) -> i64 {
    return asm aarch64(i64, "add $0, $1, $2", lhs, rhs);
}

#[x86-64]
fn arch_sum(lhs: i64, rhs: i64) -> i64 {
    return asm x64(i64, "movq $1, $0\naddq $2, $0", lhs, rhs);
}
```

## Conditional Regions

For `#ifdef`-style source filtering, MLang supports raw conditional regions
that are removed before parsing.

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

Use these regions when non-matching code should be ignored completely by the
compiler, for example with duplicate definitions or target-specific asm.

## Type Name Property (`.name`)

Values expose a read-only synthetic `.name` property for logging static type
names:

```mla
let i: i32 = 12;
println!("{}", i.name);  // i32
```

For collection values, the returned name includes inner types when available:
- `list<i32>`
- `array<i32, 4>`
- `map<str8, i32>`

If a struct defines a real field named `name`, normal field access is used
instead of the synthetic type-name property.

## Typed `var` Declarations Without Initializers

A typed `var` declaration with no initializer is zero-initialized.

Examples:

```mla
var count: i32;          // 0
var count2: i32 {};      // 0
var ready: bool;         // false
var ready2: bool {};     // false
var next: ptr<i32>;      // null
var next2: ptr<i32> {};  // null
```

Dereferencing a raw pointer requires `unsafe`, but `unsafe` does not disable
null checks. A pointer known to be null is rejected at compile time; unknown raw
pointers are checked at runtime before load or store.

Struct values follow the same rule: every field starts at its zero value.
The explicit `{}` form requests zero-initialized storage in the same style as
C++ value-initialization.

```mla
struct PairStamp {
    var left: i64;
    var right: i64;
};

fn main() -> i32 {
    var stamp: PairStamp;
    var copy: PairStamp {};
    return (stamp.left == 0 && copy.right == 0) ? 0 : 1;
}
```

For derived structs, zero-initialization covers both the derived fields and the
inherited base fields.

This is equivalent to explicit zero/default construction for the declared
storage. `let` declarations are different: they still require an initializer.

The compiler warns when zero-initialization is implicit:

```mla
var stamp: PairStamp;    // warning: implicit zero-initialization
var copy: PairStamp {};  // no warning
```

Use `{}` when you want the zero-initialization intent to be explicit in source.

## `cexpr` Compile-Time Evaluation

MLang supports explicit compile-time evaluation with the `cexpr` keyword.

Use `cexpr(expr)` when an expression must be folded during compilation:

```mla
fn main() -> i32 {
    let mask: i64 = cexpr((1 << 5) | 3);
    return mask == 35 ? 0 : 1;
}
```

Functions can opt in to compile-time calls with `cexpr fn`:

```mla
cexpr fn twice(x: i32) -> i32 {
    return x * 2;
}

fn main() -> i32 {
    static_assert!(twice(21) == 42);
    let value: i32 = cexpr(twice(21));
    return value == 42 ? 0 : 1;
}
```

The same function may also be written with a postfix specifier:

```mla
fn twice(x: i32) cexpr -> i32 {
    return x * 2;
}
```

`cexpr fn` also supports first-version floating-point arithmetic:

```mla
cexpr fn midpoint(a: f32, b: f32) -> f32 {
    return (a + b) / 2.0f;
}

fn main() -> i32 {
    static_assert!(midpoint(2.0f, 4.0f) == 3.0f);
    let value: f32 = cexpr(midpoint(8.0f, 10.0f));
    return value > 8.9f && value < 9.1f ? 0 : 1;
}
```

Compile-time functions can be generic. Type parameters are declared with
`generic<T, Y>` before `cexpr fn`, and `type_id(T)` can be used in `cexpr if`
conditions to select branches by the concrete compile-time argument type:

```mla
alias SomeType = i64;
alias SomeOtherType = f64;

struct Marker {
    var value: i32;
};

generic<T, Y>
cexpr fn pick(item: T, item2: Y) {
    cexpr if type_id(T) == SomeType {
        return item * 2;
    } else if type_id(T) == SomeOtherType {
        return 30;
    } else if type_id(T) == Marker {
        return 7;
    } else {
        return item2;
    }
}
```

Struct arguments are supported for this type-dispatch use case: the compiler
tracks the argument's type, not its fields. Trying to materialize a struct value
with `cexpr(marker)` or read struct fields during compile-time evaluation is a
compile-time error.

Compile-time values can be declared with `cexpr name: Type = expr;`. The
initializer must be compile-time evaluable:

```mla
cexpr Steps: i32 = 21;
cexpr Gain: f32 = 1.5f;

fn main() -> i32 {
    static_assert!(Steps * 2 == 42);

    cexpr Local: i32 = Steps * 2;
    let value: i32 = cexpr(Local);
    return value == 42 ? 0 : 1;
}
```

`cexpr if` selects a branch during compilation. The condition must be
compile-time evaluable, and only the selected branch is lowered:

```mla
cexpr UseFastPath: bool = true;

fn main() -> i32 {
    cexpr if UseFastPath {
        return 0;
    } else if false {
        return missing_other_symbol();
    } else {
        return missing_runtime_symbol();
    }
}
```

Current first-version constraints:
- `cexpr` currently supports integer, floating-point, and `bool` values.
- `cexpr fn` bodies support compile-time-evaluable expressions, local
  `let`/`var` declarations, assignment, `if`/`else`, nested blocks, and
  `return`.
- `generic<T, ...> cexpr fn` supports one or more type parameters for
  compile-time calls, with `type_id(T)` usable in compile-time conditions.
- Struct arguments can participate in `type_id(T)` dispatch, but struct fields
  and struct constants are not compile-time evaluable yet.
- `cexpr if` currently supports block bodies, `else if` chains, and an
  optional `else` block.
- Calling a non-`cexpr fn` from `cexpr(...)` is rejected.

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

String-backed enums are also supported with `str8`:

```mla
enum HttpMethod : str8 {
    Get = "GET",
    Post = "POST",
    Patch = "PATCH",
};
```

Compatibility and diagnostics:
- Values must fit the declared backing type.
- Compiler errors include `file.mla:row:column` locations for invalid values.
- Compatible enum values can be referenced across enums when representable in
  the target enum backing type.
- Nested enum declarations (`Outer::Inner`) are supported.
- `str8`-backed enums support string literal values and same-enum value reuse.
- String-backed enums can be compared with `str8`, matched, switched on,
  iterated, printed, and reassigned.

Hex integer literals are accepted in expressions and enum values:

```mla
enum ErrorMask : u32 {
    None = 0x00,
    Retry = 0x10,
    Fatal = 0x2A,
};
```

See also:
- `examples/enum_print_demo.mla`
- `examples/enum_string_hex_demo.mla`

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

Fixed-capacity arrays use `array<T, N>`. They use the same list-compatible
runtime operations, but literal and fill initializers are checked against `N` at
compile time:

```mla
let fixed: array<int, 6> = {1, 3, 4, 5, 6, 7};
var scratch: array<int, 6> = {};
scratch.push(10);
scratch.push(20);
scratch.extend(vec![30, 40]);
scratch.fill(1);

// Compile-time error: initializer has 4 elements but capacity is 3.
let too_many: array<int, 3> = {1, 2, 3, 4};
```

Array growth is checked before writing. When the compiler can prove the current
length and source length, `push` and `extend` overflow is a compile-time error.
Unknown runtime-sized sources keep the runtime capacity guard. `fill(value)`
sets the array length to exactly `N`.

Indexing is checked too. Constant indexes that are known to be out of bounds are
compile-time errors, while dynamic indexes keep a runtime bounds guard before
loading.

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

## `bit` and `size_of`

MLang provides a builtin `bit` type for logical `0` / `1` values:

```mla
var state: bit = 0;
state = 1;
state = bit(0);
```

`bit` is useful for flags stored in structs and other low-level state. For
readable aliases you can wrap the two values in helpers such as
`std::bits::ON()` and `std::bits::OFF()`.

The builtin `size_of(...)` returns byte sizes as `i64`:

```mla
println!("bit={} bool={} header={}",
         size_of(bit), size_of(bool), size_of(list<bool>));
```

Supported forms:
- `size_of(Type)`
- `size_of(expr)`

For `array<T, N>`, `size_of` returns fixed storage bytes: `N * size_of(T)`.

When the size can be resolved at compile time, `size_of` can also be used
inside `static_assert!`:

```mla
let view: Span<i32> = [1, 2, 3];
static_assert!(size_of(view) == size_of(list<i32>));
static_assert!(size_of(array<int, 6>) == 24);
```

Important distinction:
- `list<bool>` is a normal list container with ordinary element storage
- `std::bitset::BitSet` stores values densely at one bit per entry

## Builder Syntax (`add<T>(...)` and clause keys) {#builder_syntax}

MLang has a declarative builder syntax for constructing object trees that
serialize well (for example to JSON via `{:json}` / `{:#json}`). A builder
expression starts with `add<T>()` and chains clauses using the `|`
pipe operator. Every type name used in the expression must be declared
first — the compiler enforces that the "functional-looking" types in a
builder are real types with real fields.

### Forms

- `add<T>()` — starts a builder producing a value of container type `T`.
- `Name{value}` — a clause that sets a single typed field. `Name` is either
  a full `struct Name { ... }` declaration with exactly one field (the field
  is conventionally called `value`), or a compact `field Name: Type;`
  declaration (see below).
- `add<Child>(clause, clause, ...)` — a nested builder that becomes a child
  field in the enclosing object. The child field's name is derived from its
  `Name{"..."}` clause when present, otherwise from the lowercased child
  type hint.

All three are chained together with `|`:

```mla
add<Outer>()
    | ClauseA{valueA}
    | add<Inner>(ClauseB{valueB})
```

### Declaring clause types

**Option A — full `struct` declaration** (explicit, works for any number of
fields):

```mla
struct Method   { var value: str8; };
struct Url      { var value: str8; };
struct Priority { var value: i32;  };
```

**Option B — `field` keyword** (compact, desugars to a single-field struct
named `value`):

```mla
field Method:   str8;
field Url:      str8;
field Priority: i32;
```

`field Foo: T;` is exactly equivalent to
`struct Foo { var value: T; };` with `#[derive(Debug)]` applied. Use `pub
field Foo: T;` to export the desugared struct.

### Value types

Builder clause values are not limited to strings. The compiler checks the
supplied value against the clause's declared field type. The following are
supported:

- Strings (`str8` / `str16` / `string`).
- All integer widths (`i8`..`i64`, `u8`..`u64`).
- Floats (`f32`, `f64`).
- Declared structs — e.g. passing an `add<Inner>(...)` or a struct literal
  to a clause whose field type is a declared struct.

Numeric literals widen across integer widths (`i64` literal → `i32` field
is accepted), and string literals widen across string encodings.

### Worked example — full `struct` form

```mla
struct Method   { var value: str8; };
struct Url      { var value: str8; };
struct Name     { var value: str8; };
struct Value    { var value: str8; };
struct Text     { var value: str8; };
struct Priority { var value: i32;  };

struct Header { var name: str8; var value: str8; };
struct Body   { var text: str8; };
struct HttpRequest {
    var method:   str8;
    var url:      str8;
    var body:     Body;
    var priority: i32;
};

fn main() -> i32 {
    let request = add<HttpRequest>()
        | Method{"POST"}
        | Url{"https://api.example.com/v1/messages"}
        | Priority{7}
        | add<Body>(Text{"hello"})
        | add<Header>(Name{"Authorization"}, Value{"Bearer token-123"})
        | add<Header>(Name{"ContentType"}, Value{"application/json"});

    println!("{:#json}", request);
    return 0;
}
```

See @ref examples/builder_object_json_demo.mla "builder_object_json_demo.mla"
for the complete runnable program.

### Worked example — `field` form

```mla
field Id:       i32;
field Customer: str8;
field Notes:    str8;
field Name:     str8;
field Price:    f64;
field Quantity: i32;

struct LineItem {
    var name:     str8;
    var price:    f64;
    var quantity: i32;
};

struct Order {
    var id:       i32;
    var customer: str8;
    var notes:    str8;
    var items:    list<LineItem>;
};

fn main() -> i32 {
    let order = add<Order>()
        | Id{1001}
        | Customer{"Alice"}
        | Notes{"gift wrap"}
        | add<LineItem>(Name{"widget"}, Price{9.99},  Quantity{2})
        | add<LineItem>(Name{"gizmo"},  Price{19.95}, Quantity{1});

    println!("{:#json}", order);
    return 0;
}
```

See @ref examples/builder_object_field_demo.mla
"builder_object_field_demo.mla".

## Property Fields (`@property`) {#property_fields}

Struct fields may be annotated with `@property` to synthesize getter/setter
methods and optionally constrain direct backing-field access.

Examples:

```mla
struct Device {
    @property(hidden) var value: i32;
};
```

```mla
struct Base {
    @property(protected) var value: i32;
};

struct Child : Base {
    fn read(self: Child) -> i32 {
        return self.value;
    }
};
```

See @ref language_attributes "@property(...)" in the attributes guide for the
full option list: `atomic`, `mutex`, `recursive`, `hidden`, and `protected`.

If the enclosing struct also uses `#[derive(Json)]`, property-backed fields are
serialized as normal object fields and a sibling `@property` metadata subtree
is emitted to describe the active property options (`hidden`, `protected`,
`atomic`, `mutex`, `recursive`).

### Diagnostics

The compiler rejects ill-formed builders at parse time. Common mistakes:

- `add<Undeclared>()` — `struct Undeclared` has not been declared yet
  (MLANG-E1014).
- `Method{42}` when `field Method: str8;` — value type mismatch
  (MLANG-E1014 — `"builder clause 'Method' expects value of type 'str8'
  but got 'i64'"`).
- `struct TwoFields { var a: str8; var b: str8; };` followed by
  `TwoFields{"x"}` — clause structs must have exactly one field
  (MLANG-E1014).
- `add<T>` without the `<T>` — missing type argument (MLANG-E1015).

Because validation is parse-order sensitive, **put type declarations above
the builder expressions that reference them** (typically at the top of the
file).

### The nested-sibling name-collision rule

Each nested `add<Child>(...)` becomes a field in its parent. The field name
is taken from the child's `Name{"..."}` clause, falling back to the
lowercased child type hint. Two siblings of the same type therefore need
distinct `Name{...}` clauses, otherwise parsing fails with MLANG-E1010
(`"duplicate or conflicting field path"`). In the HTTP example, the two
`add<Header>` siblings are distinguished by `Name{"Authorization"}` vs.
`Name{"ContentType"}`.
