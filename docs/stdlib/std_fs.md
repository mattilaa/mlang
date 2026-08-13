# std::fs

Module file: `stdlib/std/fs.mla`

### Directory helpers
- `file_exists(path: str8) -> i32`
- `is_dir(path: str8) -> i32`
- `parent_dir(path: str8) -> str8`
- `cwd() -> str8`
- `chdir(path: str8) -> i32`
- `mkdir_p(path: str8) -> i32`
- `remove_tree(path: str8) -> i32`
- `list_dir(path: str8) -> list<str8>`
- `glob_recursive(root: str8, pattern: str8) -> list<str8>`

## Additional std::fs API

Module file: `stdlib/std/fs.mla`

### Types
- `file`
- `buf_reader`

### file API
- `file::open(path: str8) -> result<file, str8>`
- `file::create(path: str8) -> result<file, str8>`
- `file::close(self: file) -> i32`
- `file::write(self: file, s: str8) -> result<i64, str8>`
- `file::write_line(self: file, s: str8) -> result<i64, str8>`
