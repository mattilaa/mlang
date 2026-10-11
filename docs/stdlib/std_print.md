# std::print

Module file: `stdlib/std/print.mla`

Mlang's C++23-inspired output module. Format values with the `format!`
expression, then pass the resulting `str8` to an output function. This keeps
formatting type-aware while respecting Mlang's non-variadic function model.

```mla
mod std::print;
use std::print::*;

print(format!("value = {}", value));
println(format!("answer = {}", 42));
eprintln(format!("error: {}", message));
```

### API

- `print(text)` writes to stdout without a newline.
- `println(text)` writes to stdout and appends a newline.
- `eprint(text)` and `eprintln(text)` provide the same behavior on stderr.
- `print_to(fd, text)` and `println_to(fd, text)` write to descriptor `fd`;
  descriptors 1 and 2 refer to stdout and stderr.

Unlike C++23 `std::print`, these functions receive a preformatted string.
Use Mlang's `format!` expression for substitutions and format specifications.
