# mlang
MLang - Programming Language

## LSP
Minimal LSP server lives at `tools/mlang_lsp/mlang_lsp.py` (stdio).

```sh
python3 tools/mlang_lsp/mlang_lsp.py --stdio
```

## C++ LSP
C++ LSP server target is `mlangd` (stdio).

```sh
cmake -S . -B build
cmake --build build
./build/mlangd --stdio
```

Use with UVim:

```sh
uvim --mlang-lsp --mlang-lsp-path ./build/mlangd
```

`mlang` emits `mlang_commands.json` for editor tooling. You can add module
search paths in `mlang.toml`:

```toml
[tool.mlang]
module_paths = ["modules", "vendor/mlang"]
```

## Build + Install
Build and install both `mlang` and `mlangd`:

```sh
./scripts/build_install.sh
```

Build and install only `mlangd`:

```sh
./scripts/build_install_lsp.sh
```

Use Make instead of Ninja:

```sh
./scripts/build_install.sh --use-make
./scripts/build_install_lsp.sh --use-make
```

## Testing
Run tests with the built-in runner. Mark test functions with `#[test]` and
return `0` for pass, non-zero for failure.

```sh
# Run tests in the ./tests directory
mlang test

# Run tests in a specific file or directory
mlang test tests/test_sample.mla
mlang test tests

# Alternative entry point
mlang run tests
```

Skip compiling tests in normal builds:

```sh
mlang --no-tests main.mla
```

## Rust-like Attributes
Mlang currently supports these Rust-like attributes:

| Attribute | Target | Purpose |
|---|---|---|
| `#[derive(Debug)]` | `struct` definitions | Enables debug formatting (`{:?}`/`{:#?}` and `println!(value)` for structs). |
| `#[test]` | `fn` definitions | Marks test functions discoverable by `mlang test` / `mlang run tests`. |

### `#[derive(Debug)]`

```mla
#[derive(Debug)]
struct Point {
    var x: i32;
    var y: i32;
};

fn main() -> i32 {
    let origin: Point = Point { x: 0, y: 0 };
    let expected: string = "The origin is: Point { x: 0, y: 0 }";

    assert_eq!(format!("The origin is: {origin}"), expected);
    println!(origin);
    debug!("origin = {origin}");
    return 0;
}
```

### `#[test]`
Test functions should take no parameters and return `void` or `i32`:

```mla
#[test]
fn test_addition() -> i32 {
    let x: i32 = 2 + 2;
    if x == 4: {
        return 0;
    }
    return 1;
}
```

### Related
- Full attribute notes and contributor guide: `docs/language_attributes.md`
- Runnable example: `examples/rust_attributes.mla`

Enable debug-only logging (`debug!`) with:

```sh
mlang --debug main.mla
```

## Package Manager (C++)
Example of calling the package manager from code:

```cpp
#include "package_manager.h"

int main(int argc, char** argv) {
    PackageManager pkg;
    return pkg.run(argc, argv);
}
```

`mlang.toml` example with pkg-config C dependencies:

```toml
[package]
name = "curl_demo"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]

[c-dependencies]
curl = { pkg_config = "libcurl" }
```
