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

## Debug Formatting + Assertions
Derive Debug for structs, use `format!` with `{:?}`/`{:#?}`, and `assert_eq!`:

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

Enable debug-only logging (`debug!`) via:

```sh
mlang --debug main.mla
```
