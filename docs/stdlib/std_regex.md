# std::regex

Module file: `stdlib/std/regex.mla`

### Types
- `regex`

### Compile / lifetime
- `regex::compile(pattern: str8) -> Result<regex, str8>`
- `regex::close(self: regex) -> i32`

### Matching
- `regex::is_match(self: regex, text: str8) -> i32`
- `regex::find_start(self: regex, text: str8) -> i64`
- `regex::find_end(self: regex, text: str8) -> i64`
- `regex::match_start(self: regex, text: str8, group_index: i64) -> i64`
- `regex::match_end(self: regex, text: str8, group_index: i64) -> i64`

### Errors
- `last_error() -> str8`
