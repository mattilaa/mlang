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

UVim `<leader>f` formatting works through `textDocument/formatting`; `mlangd`
now uses `mlang-format` and `.mlang-format` (`--style=file`) for buffer
formatting.

Enable LSP debug telemetry (`cache clears`, `active docs`, `evictions`):

```sh
MLANG_LSP_DEBUG=1 ./build/mlangd --stdio
```

Write debug output to a file:

```sh
MLANG_LSP_DEBUG=1 MLANG_LSP_DEBUG_LOG=/tmp/mlangd_telemetry.log ./build/mlangd --stdio
```

Tune semantic cache clear cadence for long-running sessions:

```sh
MLANGD_COMPILER_CACHE_CLEAR_INTERVAL=180 ./build/mlangd --stdio
```

## Mlangd (Mlang Scaffold)
Initial Mlang implementation scaffold lives at `tools/mlangd-mla/main.mla`.
It uses `std::jsonrpc::run_stdio_loop(...)` and a Mlang dispatcher hook:
`__mlang_std_jsonrpc_runtime_dispatch(...)`.

Build object:

```sh
./build/mlang -c tools/mlangd-mla/main.mla -L ./build -lmlang_std
```

Build executable:

```sh
./build/mlang tools/mlangd-mla/main.mla -L ./build -lmlang_std -o /tmp/mlangd-mla
/tmp/mlangd-mla --stdio
```

`mlang` emits `mlang_commands.json` for editor tooling. You can add module
search paths in `mlang.toml`:

```toml
[tool.mlang]
module_paths = ["modules", "vendor/mlang"]
```

## Stdlib Linking
When using stdlib modules backed by native code (e.g. `std::math`), link
against the stdlib library just like GCC/Clang:

```sh
mlang main.mla -L ~/.local/lib -lmlang_std
```

You can also set a default search path:

```sh
export MLANG_STDLIB_LIB_PATH=~/.local/lib
```

The stdlib module search path is controlled by `MLANG_STDLIB_PATH` and defaults
to `~/.local/share/mlang/stdlib` when installed.

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

## Formatter
`mlang-format` is now a separate binary compiled from Mlang source:
`tools/mlang-format-mla/main.mla` (port in progress from Python).
It supports clang-format style invocation and reads `.mlang-format`
from the file/workspace hierarchy.

```sh
# Print formatted output
mlang-format path/to/file.mla

# In-place rewrite (clang-format style)
mlang-format -i path/to/file.mla

# From stdin with style lookup anchored to a file path
cat path/to/file.mla | mlang-format --assume-filename path/to/file.mla
```

Current Mlang port scope:
- `--style file`, `-i/--in-place`, `--root`, `--assume-filename`
- `.mlang-format`: `IndentWidth`, `EnsureTrailingNewline`,
  `SpaceAfterComma`, `SpaceAfterColon`, `SpaceAroundOperators`,
  `SpaceInsideBracesSingleLine`, `CompactFatArrow` (default: `true`)

## Quickstart (Package Manager + curl example)
Build `mlang`, fetch a git dependency with the package manager, compile the
example, and run it (the program uses libcurl to fetch a URL).

```sh
# Build compiler first
./scripts/build_install.sh --no-install

# Run package-manager demo
cd examples/package_manager_git_cjson
../../build/mlang pkg fetch
../../build/mlang pkg build
# Or: ../../build/mlang pkg build -O3
./build/cjson_demo
# Optional URL override:
# ./build/cjson_demo --url https://www.someplace.com
```

Prereqs for this example:
- `git`, `cmake`, C toolchain
- `pkg-config`
- `libcurl` development package

See `examples/package_manager_git_cjson/README.md` for distro-specific install
commands.

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

Interactive `std::io` input example (manual run, not part of Robot example
suite): `examples/std_io_input_demo.mla`.
The example uses `var user_input` directly (no `&mut` required) and trims with
`trim(user_input)`.
`std::io` now also supports stderr writes, stream buffering configuration
(`set_*_buffering`), and non-blocking stdin polling (`read_line_nonblocking`).
Trait-like `std::io` handles (`Read`, `Write`, `Seek`, `BufRead`) example:
`examples/std_io_traits_demo.mla`.
Filesystem API (`std::fs::File`, `std::fs::BufReader`) example:
`examples/std_fs_demo.mla`.
TCP networking API (`std::net::TcpListener`, `std::net::TcpStream`,
non-blocking mode, read/write timeouts) example:
`examples/std_net_demo.mla`.
JSON API (`std::json::JsonDoc` parse/stringify/object-array navigation, iterators, and `from_file`) example:
`examples/std_json_demo.mla`.
JSON-RPC/LSP transport runtime (`std::jsonrpc` Content-Length framing, timeout reads, cancellation registry, queue runtime) example:
`examples/std_jsonrpc_runtime_demo.mla`.
Manual stdio JSON-RPC worker runtime demo (`run_stdio_loop`, built-in `$/cancelRequest` routing):
`examples/std_jsonrpc_stdio_loop_demo.mla` (manual run, not part of Robot suite).
Incremental parse/query API for tooling (`std::compiler::Session`, open/change/close, diagnostics, hover, completion, document symbols, cross-document definition via `mod` files) example:
`examples/std_compiler_demo.mla`.
`?` is supported for `Result` propagation (early-return on `Err`).

## Examples
- Scope-exit destructor + owned resource cleanup:
  `examples/scope_exit_drop_demo.mla`
- Trait-like IO (`Read`/`Write`/`Seek`/`BufRead`) using in-memory cursor:
  `examples/std_io_traits_demo.mla`
- Filesystem read-lines flow (`File::open`, `BufReader::new`, `lines`):
  `examples/std_fs_demo.mla`
- TCP loopback client/server over libc sockets:
  `examples/std_net_demo.mla`
- JSON parse/stringify, navigation, iterators, and `from_file` (`JsonDoc`, `JsonValue`):
  `examples/std_json_demo.mla`
- JSON-RPC/LSP stdio transport + cancellation/runtime queues (`std::jsonrpc`):
  `examples/std_jsonrpc_runtime_demo.mla`
- JSON-RPC stdio worker runtime loop (`std::jsonrpc::run_stdio_loop`):
  `examples/std_jsonrpc_stdio_loop_demo.mla`

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
