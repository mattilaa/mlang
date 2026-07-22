# std::exceptions

Module file: `stdlib/std/exceptions.mla`

This module provides the runtime payload type used by the language-level
`throw` and `try/catch` syntax.

### Types
- `Exception`

### Language usage

Basic throw/catch:

```mla
mod std::exceptions;
use std::exceptions::*;

fn parse_number(text: str8) -> i32 {
    if text == "42" {
        return 42;
    }
    throw with_line("ParseError", "expected 42", 12);
}

fn main() -> i32 {
    try {
        let value: i32 = parse_number("x");
        println!("{}", value);
    } catch e: Exception {
        println!("caught {} at {}: {}", e.type_name, e.source_line, e.message);
    }
    return 0;
}
```

Notes:
- `throw expr;` transfers control to the nearest enclosing `catch`.
- `catch e: Exception` binds the thrown payload for the handler block.
- If no handler is active, the runtime prints the uncaught exception and aborts.
- Scope-owned values are cleaned up during unwind before control reaches `catch`.

### API
- `Exception`
  Fields:
  - `type_name: str8`
  - `message: str8`
  - `source_line: i32`
  - `owned: bool`
- `new(type_name: str8, message: str8) -> Exception`
- `with_line(type_name: str8, message: str8, source_line: i32) -> Exception`
- `free(ex: Exception) -> void`
