# Multi-Bin Package Example

This example shows two package-manager features together:

- multiple executable targets via `[[bin]]`
- target-scoped build config that overrides package defaults

The manifest declares package defaults in `[tool.mlang]` and then defines two
bin targets:

```toml
[tool.mlang]
min_mlang_version = "0.2.0"
opt_level = "O2"
compiler_flags = ["-Wno-unwrap"]

[[bin]]
name = "hello"
entry = "src/hello.mla"

[[bin]]
name = "inspect"
entry = "src/inspect.mla"
opt_level = "O0"
linker_flags = ["-Wl,-dead_strip"]
```

`mlang pkg build` builds both outputs into `build/`:

- `build/hello`
- `build/inspect`

The `inspect` target overrides `opt_level` and adds an extra linker flag,
while still inheriting package-level defaults such as `min_mlang_version` and
`compiler_flags`.

## Run

From this directory:

```sh
../../build/mlang pkg build
./build/hello
./build/inspect
../../build/mlang pkg clean
```
