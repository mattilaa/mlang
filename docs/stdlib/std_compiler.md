# std::compiler

Module file: `stdlib/std/compiler.mla`

### Types
- `session`
- `syntax_diagnostic`
- `document_symbol`
- `definition_location`
- `reference_location`
- `resolved_symbol`

### Global helpers
- `session_create() -> result<session, str8>`
- `last_status() -> i32`
- `status_name(status: i32) -> str8`
- `version() -> str8`
- `last_error() -> str8`

### session lifecycle and document state
- `session::destroy(self: session) -> result<i32, str8>`
- `session::open_document(self: session, uri: str8, language_id: str8, text: str8, version: i32) -> result<i32, str8>`
- `session::change_document(self: session, uri: str8, text: str8, version: i32) -> result<i32, str8>`
- `session::close_document(self: session, uri: str8) -> result<i32, str8>`

### Diagnostics and editor queries
- `session::syntax_diagnostic_count(self: session, uri: str8) -> result<i32, str8>`
- `session::syntax_diagnostic_get(self: session, uri: str8, index: i32) -> result<syntax_diagnostic, str8>`
- `session::hover(self: session, uri: str8, line: i32, column: i32) -> result<str8, str8>`
- `session::completion_count(self: session, uri: str8, line: i32, column: i32) -> result<i32, str8>`
- `session::completion_get(self: session, uri: str8, line: i32, column: i32, index: i32) -> result<str8, str8>`

### Symbols, definitions, references, rename
- `session::document_symbol_count(self: session, uri: str8) -> result<i32, str8>`
- `session::document_symbol_get(self: session, uri: str8, index: i32) -> result<document_symbol, str8>`
- `session::definition(self: session, uri: str8, line: i32, column: i32) -> result<definition_location, str8>`
- `session::references_count(self: session, uri: str8, line: i32, column: i32) -> result<i32, str8>`
- `session::reference_get(self: session, uri: str8, line: i32, column: i32, index: i32) -> result<reference_location, str8>`
- `session::resolve_symbol(self: session, uri: str8, line: i32, column: i32) -> result<resolved_symbol, str8>`
- `session::rename_is_safe(self: session, uri: str8, line: i32, column: i32, new_name: str8) -> result<i32, str8>`
- `session::semantic_cache_warm(self: session, uri: str8) -> result<i32, str8>`
- `session::semantic_cache_clear(self: session) -> result<i32, str8>`

### Concept symbols and builtin navigation

`document_symbol.kind` uses `1` for functions, `2` for variables, `3` for
structs, `4` for modules, `5` for type aliases, and `6` for concepts. Concept
symbols include their predicate signature and declaration documentation.
Definition queries resolve local concepts, visible imports, and specific import
aliases to their original declaration locations. Private concepts remain local
to their defining module.

Builtin keywords such as `where` and `req` resolve to the matching reference
entry in `stdlib/types.mla`, with that file's URI and source location. Imported
standard concepts resolve to `stdlib/std/concepts.mla`. Qualified module names
retain their path when finding a provider, so a user file named `concepts.mla`
does not hide `std::concepts`.
