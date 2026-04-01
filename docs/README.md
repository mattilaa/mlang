# MLang Documentation

MLang is a systems programming language built to combine the parts of C, C++,
and Rust that are most useful in practice into one language.

The project goal is straightforward:
- C-style directness and native interop
- C++-style zero-cost abstractions, templates/generics, and low-level control
- Rust-inspired safety features such as explicit ownership, `Result`, `Option`,
  `match`, bounds checks, and stronger compile-time diagnostics

This documentation is split into a small guided entry path and deeper reference
pages.

## What MLang Is

MLang is intended for native programs, tooling, CLIs, servers, and experiments
where you want:
- direct access to C libraries and platform APIs
- predictable generated native code
- a practical standard library
- safer defaults than plain C/C++
- modern language features without giving up low-level control

Examples of supported language/runtime features include:
- structs, enums, generics, modules, and `use type` aliases
- `Option<T>`, `Result<T, E>`, `match`, and functional pipe `|>`
- exceptions with `throw` and `try/catch`
- RAII-style scope cleanup
- `switch` / `case`
- `bit`, `sizeof(Type)`, and `sizeof(expr)`
- inline assembly
- `Vec`, `list<T>`, `span<T>`, `HashMap`, regex, JSON, IO, networking, threads
- direct C interop through `extern fn`

## Start Here

If you are new to MLang, read these pages in this order:

1. [Quick Guide](quick_guide.md)
2. [Language Syntax](language_syntax.md)
3. [Language Attributes](language_attributes.md)
4. [Stdlib Module API](stdlib_mlang_api.md)

## Quick Links

- [Quick Guide](quick_guide.md)
  A short introduction with small examples and “how do I write code in this
  language?” answers.
- [Language Syntax](language_syntax.md)
  Language features such as `switch`, exceptions, pipe syntax, inline asm,
  lambdas/folds, and `bit`/`sizeof`.
- [Language Attributes](language_attributes.md)
  Attribute syntax such as `#[test]`, `#[derive(Debug)]`, and `#[inline]`.
- [Stdlib Module API](stdlib_mlang_api.md)
  Module-by-module API documentation for the MLang standard library.
- [Ownership Model Notes](ownership_model.h)
  Current ownership and move/copy model notes used by the compiler.

## First Example

```mla
fn main() -> i32 {
    let values: list<i32> = [1, 2, 3, 4];
    var total: i32 = 0;
    for v in values {
        total = total + v;
    }
    println!("total={}", total);
    return 0;
}
```

This shows the common MLang style:
- explicit primitive types such as `i32`
- `let` for immutable bindings and `var` for mutable state
- standard container support with `list<T>`
- normal native executable output

## C Interop Example

```mla
extern fn puts(text: str8) -> i32;

fn main() -> i32 {
    puts("hello from libc");
    return 0;
}
```

MLang is designed to interoperate with existing C APIs directly. For larger
examples, see the example programs in the repository such as:
- `examples/c_lib_file_io_demo.mla`
- `examples/c_lib_text_parse_demo.mla`

## Safety + Modern Features

MLang is not only a thin C wrapper. It also includes language features intended
to make low-level code safer and easier to reason about:

- `Result<T, E>` and `Option<T>`
- `match`
- bounds checks for strings and lists
- RAII-style cleanup on scope exit
- exceptions with cleanup-aware unwinding
- `static_assert!`
- packed `bit` fields in structs

For feature details and syntax examples, see:
- [Language Syntax](language_syntax.md)
- [Stdlib Module API](stdlib_mlang_api.md)

## Feature Guide

Use this section as a map into the existing documentation.

### Core Language

- [Language Syntax](language_syntax.md)
  Covers:
  - `use type`
  - `if`, `while`, guarded forms
  - `switch` / `case`
  - exceptions
  - functional pipe `|>`
  - inline assembly
  - lambdas and folds
  - `bit` and `sizeof`

### Testing and Benchmarking

- [Language Attributes](language_attributes.md)
  Covers:
  - `#[test]`
  - `mlang bench`
  - `#[derive(Debug)]`
  - `#[inline]`, `#[inline(always)]`, `#[inline(never)]`

### Standard Library

- [Stdlib Module API](stdlib_mlang_api.md)
  Includes documentation for:
  - strings and UTF helpers
  - IO and filesystem
  - JSON and JSON-RPC
  - math and algorithms
  - regex
  - process execution
  - networking
  - threading and synchronization
  - `Vec`, `BitSet`, `span`, `sed`, and more

### Practical Examples

Relevant example files in the repository:
- `examples/std_span_demo.mla`
- `examples/exceptions_try_catch_demo.mla`
- `examples/switch_demo.mla`
- `examples/enum_string_hex_demo.mla`
- `examples/pipe_operator_demo.mla`
- `examples/bit_packed_struct_demo.mla`
- `examples/c_lib_file_io_demo.mla`
- `examples/c_lib_text_parse_demo.mla`
- `examples/platform_inline_asm_demo.mla`

## Design Direction

The “why” behind MLang is simple: it was created to combine the best parts of
C, C++, and Rust into one language.

In practical terms that means:
- staying close to native code and native tooling
- keeping C interop simple
- allowing low-level control where needed
- adding modern safety and ergonomics where they help most

The language is intentionally pragmatic rather than ideological.

## See Also

- [Quick Guide](quick_guide.md)
- [Language Syntax](language_syntax.md)
- [Language Attributes](language_attributes.md)
- [Stdlib Module API](stdlib_mlang_api.md)
- root project `README.md` for build, test, and workflow commands
