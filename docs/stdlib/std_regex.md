# std::regex

Module file: `stdlib/std/regex.mla`

### Types
- `Regex`

### Compile / lifetime
- `Regex::compile(pattern: str8) -> Result<Regex, str8>`
- `Regex::close(self: Regex) -> i32`

### Matching
- `Regex::is_match(self: Regex, text: str8) -> i32`
- `Regex::find_start(self: Regex, text: str8) -> i64`
- `Regex::find_end(self: Regex, text: str8) -> i64`
- `Regex::match_start(self: Regex, text: str8, group_index: i64) -> i64`
- `Regex::match_end(self: Regex, text: str8, group_index: i64) -> i64`

### Errors
- `last_error() -> str8`
