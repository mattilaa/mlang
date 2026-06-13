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

1. [Quick Guide](Quick-Guide)
2. [Language Syntax](Language-Syntax)
3. [Language Attributes](Language-Attributes)
4. [Package Manager](Package-Manager)
5. [Stdlib Module API](Stdlib-Module-API)

## Quick Links

- [Quick Guide](Quick-Guide)
  A short introduction with small examples and “how do I write code in this
  language?” answers.
- [Language Syntax](Language-Syntax)
  Language features such as `switch`, exceptions, pipe syntax, inline asm,
  conditional regions, lambdas/folds, and `bit`/`sizeof`.
- [Language Attributes](Language-Attributes)
  Attribute syntax such as `#[test]`, `#[derive(Debug)]`,
  `#[derive(Json)]`, and `#[inline]`.
- [Stdlib Module API](Stdlib-Module-API)
  Module-by-module API documentation for the MLang standard library.
- [Package Manager](Package-Manager)
  `mlang pkg` workflow, subcommands, manifest layout, and package build
  configuration keys.
- [UML UI Generator Example](UML-UI-Generator)
  TOML schema and sample files for the PNG UML example renderer.
- [Ownership Model Notes](https://github.com/mattilaa/mlang/blob/main/docs/ownership_model.h)
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
- [Language Syntax](Language-Syntax)
- [Stdlib Module API](Stdlib-Module-API)

## Feature Guide

Use this section as a map into the existing documentation.

### Core Language

- [Language Syntax](Language-Syntax)
  Covers:
  - `use type`
  - `if`, `while`, guarded forms
  - `switch` / `case`
  - exceptions
  - functional pipe `|>`
  - inline assembly
  - conditional source regions
  - lambdas and folds
  - `bit` and `sizeof`

### Testing and Benchmarking

- [Language Attributes](Language-Attributes)
  Covers:
  - `#[test]`
  - `#[fixture]` impl blocks with per-test `setup` / `teardown` hooks
  - `mlang bench`
  - `--filter` for running individual tests
  - `#[derive(Debug)]`
  - `#[derive(Json)]`
  - `#[inline]`, `#[inline(always)]`, `#[inline(never)]`
- Example files:
  - [`tests/test_sample.mla`](https://github.com/mattilaa/mlang/blob/main/tests/test_sample.mla) — unit test example
  - [`tests/fixture_tests.mla`](https://github.com/mattilaa/mlang/blob/main/tests/fixture_tests.mla) — `#[fixture]` impl with `setup` / `teardown`
  - [`tests/expect_call_tests.mla`](https://github.com/mattilaa/mlang/blob/main/tests/expect_call_tests.mla) — EXPECT_CALL-style mock cardinality and return queues
  - [`tests/bench_stdlib.mla`](https://github.com/mattilaa/mlang/blob/main/tests/bench_stdlib.mla) — benchmark example

### Standard Library

- [Stdlib Module API](Stdlib-Module-API)
  Includes documentation for:
  - strings and UTF helpers
  - IO and filesystem
  - JSON and JSON-RPC
  - math and algorithms
  - regex
  - process execution
  - networking
  - threading and synchronization
  - terminal truecolor image rendering (`std::image`)
  - `Vec`, `BitSet`, `span`, `sed`, and more

### Practical Examples

Relevant example files in the repository:
- [`examples/std_span_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/std_span_demo.mla)
- [`examples/exceptions_try_catch_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/exceptions_try_catch_demo.mla)
- [`examples/switch_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/switch_demo.mla)
- [`examples/enum_string_hex_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/enum_string_hex_demo.mla)
- [`examples/pipe_operator_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/pipe_operator_demo.mla)
- [`examples/bit_packed_struct_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/bit_packed_struct_demo.mla)
- [`examples/c_lib_file_io_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/c_lib_file_io_demo.mla)
- [`examples/c_lib_text_parse_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/c_lib_text_parse_demo.mla)
- [`examples/platform_inline_asm_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/platform_inline_asm_demo.mla)
- [`examples/image_truecolor_demo.mla`](https://github.com/mattilaa/mlang/blob/main/examples/image_truecolor_demo.mla)
- [`tests/test_sample.mla`](https://github.com/mattilaa/mlang/blob/main/tests/test_sample.mla) — unit test example
- [`tests/bench_stdlib.mla`](https://github.com/mattilaa/mlang/blob/main/tests/bench_stdlib.mla) — benchmark example
- [`examples/mlang_attributes.mla`](https://github.com/mattilaa/mlang/blob/main/examples/mlang_attributes.mla) — `#[test]` and `#[derive(Debug)]` in one file
- [`examples/test_fixture_example.mla`](https://github.com/mattilaa/mlang/blob/main/examples/test_fixture_example.mla) — `#[fixture]` impl with per-test `setup` / `teardown`
- [`examples/testing_mock_example.mla`](https://github.com/mattilaa/mlang/blob/main/examples/testing_mock_example.mla) — mock-based testing with `std::testing`
- [`examples/expect_call_example.mla`](https://github.com/mattilaa/mlang/blob/main/examples/expect_call_example.mla) — EXPECT_CALL cardinality + programmed return values
- [`examples/uml_ui_generator`](https://github.com/mattilaa/mlang/blob/main/examples/uml_ui_generator) — TOML-driven UML PNG renderer example

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

- [Quick Guide](Quick-Guide)
- [Language Syntax](Language-Syntax)
- [Language Attributes](Language-Attributes)
- [Package Manager](Package-Manager)
- [UML UI Generator Example](UML-UI-Generator)
- [Stdlib Module API](Stdlib-Module-API)
- root project `README.md` for build, test, and workflow commands
