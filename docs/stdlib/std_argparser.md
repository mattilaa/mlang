# std::argparser

Module file: `stdlib/std/argparser.mla`

### Types
- `ArgParser`
- `ParseResult`

### Parser setup
- `ArgParser::new(prog: str8, desc: str8) -> ArgParser`
- `ArgParser::flag(self: ArgParser, long_name: str8, short_name: str8, help: str8) -> void`
- `ArgParser::option(self: ArgParser, long_name: str8, short_name: str8, help: str8, default_val: str8) -> void`
- `ArgParser::positional(self: ArgParser, name: str8, help: str8) -> void`
- `ArgParser::parse(self: ArgParser, argc: i32, args: list<str8>) -> ParseResult`
- `ArgParser::print_help(self: ArgParser) -> void`
- `ArgParser::free(self: ArgParser) -> void`

### Parse results
- `ParseResult::ok(self: ParseResult) -> bool`
- `ParseResult::has_error(self: ParseResult) -> bool`
- `ParseResult::error(self: ParseResult) -> str8`
- `ParseResult::help_requested(self: ParseResult) -> bool`
- `ParseResult::flag(self: ParseResult, name: str8) -> bool`
- `ParseResult::get(self: ParseResult, name: str8) -> str8`
- `ParseResult::get_i64(self: ParseResult, name: str8) -> i64`
- `ParseResult::positional(self: ParseResult, idx: i64) -> str8`
- `ParseResult::positional_count(self: ParseResult) -> i64`
- `ParseResult::free(self: ParseResult) -> void`
