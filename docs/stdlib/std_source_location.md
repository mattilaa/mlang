# std::source_location

Module file: `stdlib/std/source_location.mla`

`std::source_location` captures the file, function, line, and column of the
call to `current()`, corresponding to C++20 `std::source_location`. The
compiler treats `current()` specially so it records the caller's location,
not the line inside the module implementation.

```mla
mod std::source_location;
use std::source_location::current;
use std::source_location::file_name;
use std::source_location::function_name;
use std::source_location::line;

fn report_problem(message: str8) -> void {
    let location: SourceLocation = current();
    eprintln(format!("{}:{} in {}: {}",
                     file_name(location), line(location),
                     function_name(location), message));
}
```

### API

- `current() -> SourceLocation` captures the call site's source position.
- `file_name(location)` returns the source path.
- `function_name(location)` returns the enclosing Mlang function's compiler
  name.
- `line(location)` and `column(location)` return one-based source coordinates.

The value is a snapshot; it does not retain source buffers or references.
