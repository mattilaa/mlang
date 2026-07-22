# std::env

Module file: `stdlib/std/env.mla`

### API
- `args() -> list<str8>`
- `len(values: &list<str8>) -> i64`
- `get(values: &list<str8>, index: i64) -> str8`
- `cwd() -> str8`
- `get(name: str8) -> str8`
- `println(msg: str8) -> void`
- `eprintln(msg: str8) -> void`
- `wants_help(values: list<str8>) -> i32`
- `arg_or(values: list<str8>, index: i64, fallback: str8) -> str8`
- `render_help(app: str8, overview: str8, options: list<HelpOption>, total_width: i64) -> str8`
