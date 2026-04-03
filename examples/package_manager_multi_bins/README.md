# Multi-Bin Package Example

This example shows two package-manager features together:

- multiple executable targets via `[[bin]]`
- target-scoped build config that overrides package defaults
- source dependencies fetched from both GitHub `git` and `tar.gz`

The manifest declares package defaults in `[tool.mlang]` and then defines two
bin targets plus two fetched dependencies:

```toml
[dependencies]
cjson = { git = "https://github.com/DaveGamble/cJSON.git", tag = "v1.7.18", build = "cmake", cmake_args = "CMAKE_POLICY_VERSION_MINIMUM=3.5;BUILD_SHARED_LIBS=OFF;CJSON_BUILD_SHARED_LIBS=OFF;CJSON_BUILD_STATIC_LIBS=ON" }
zlib = { url = "https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz", archive = "tar.gz", strip_components = "1", build = "cmake", cmake_args = "BUILD_SHARED_LIBS=OFF;ZLIB_BUILD_TESTING=OFF" }

[tool.mlang]
min_mlang_version = "0.2.0"
opt_level = "O2"
use_ninja = true
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

The `hello` target uses the Git-fetched `cJSON` dependency at runtime.
The `inspect` target uses the `tar.gz`-fetched `zlib` dependency at runtime,
and it also overrides `opt_level` with an extra linker flag.

This example also enables `use_ninja = true` in `[tool.mlang]`, so `pkg build`
checks that `ninja` exists before starting the build.

## Run

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
./build/hello
./build/inspect
../../build/mlang pkg clean
```

Expected output:

- `hello from multi-bin package git-value=42.000000`
- `inspect target built with tar.gz zlib=1.3.1`
