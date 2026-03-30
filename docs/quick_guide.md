# MLang Quick Guide {#quick_guide}

This page is a short “how do I start writing MLang?” guide.

For the full reference, see:
- [MLang Documentation Main Page](README.md)
- [Language Syntax](language_syntax.md)
- [Stdlib Module API](stdlib_mlang_api.md)

## Hello World

```mla
fn main() -> i32 {
    println!("hello");
    return 0;
}
```

Notes:
- `main` usually returns `i32`
- plain `fn main() {}` is also accepted and defaults to `-> i32`

## Variables

```mla
fn main() -> i32 {
    let name: str8 = "mlang";
    var count: i32 = 3;
    count = count + 1;
    println!("{} {}", name, count);
    return 0;
}
```

- `let` is immutable
- `var` is mutable

## Common Types

```mla
let n: i32 = 42;
let ok: bool = true;
let ch: bit = 1;
let text: str8 = "hello";
let utf16_text: str16 = String16::from(text);
let values: list<i32> = [1, 2, 3];
```

Common builtin/container types:
- `i32`, `i64`, `u8`, `u32`, `u64`, `f32`, `f64`
- `bool`
- `bit`
- `str8`, `str16`
- `list<T>`
- `Vec<T>`
- `span<T>` / `Span<T>`
- `Option<T>`
- `Result<T, E>`

## Control Flow

```mla
if count > 10 {
    println!("large");
} else if count > 0 {
    println!("small");
} else {
    println!("zero");
}
```

```mla
switch count {
case 0: {
    println!("zero");
}
case 1: {
    println!("one");
}
default: {
    println!("many");
}
}
```

## Functions

```mla
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

MLang also supports return-type inference for non-extern functions in many
cases. See [Language Syntax](language_syntax.md).

## Structs and Enums

```mla
struct Point {
    var x: i32;
    var y: i32;
}

enum Color {
    Red,
    Green,
    Blue,
}
```

```mla
let p = Point { x: 10, y: 20 };
println!("{} {}", p.x, p.y);
```

## Results and Match

```mla
fn divide(a: i32, b: i32) -> Result<i32, str8> {
    if b == 0 {
        return Err("divide by zero");
    }
    return Ok(a / b);
}

fn main() -> i32 {
    match divide(8, 2) {
    Ok(v) => {
        println!("value={}", v);
    }
    Err(msg) => {
        println!("error={}", msg);
    }
    }
    return 0;
}
```

## Exceptions

```mla
mod std::exceptions;

fn run() {
    throw Exception::new("ParseError", "bad token");
}

fn main() -> i32 {
    try {
        run();
    } catch e: Exception {
        println!("caught {}: {}", e.type_name, e.message);
    }
    return 0;
}
```

## C Interop

```mla
extern fn puts(text: str8) -> i32;

fn main() -> i32 {
    puts("hello from libc");
    return 0;
}
```

Use `extern fn` when calling C APIs or wrapped native libraries.

## Sizes and Static Assertions

```mla
let values: Span<i32> = [1, 2, 3];
static_assert!(sizeof(values) == sizeof(list<i32>));
println!("size={}", sizeof(values));
```

## Where To Go Next

- [Language Syntax](language_syntax.md) for language features and examples
- [Language Attributes](language_attributes.md) for `#[test]` and related attributes
- [Stdlib Module API](stdlib_mlang_api.md) for module-by-module APIs
- repository `examples/` for runnable programs
