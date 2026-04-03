# Static Package Build With Fetched C Library

This example shows how to build an `mlang pkg` executable that links a fetched
C library as a static archive.

The package fetches `cJSON` from a GitHub `tar.gz`, builds its `.a` archive
with CMake, and then links that archive directly into the final executable.

## Manifest

```toml
[dependencies]
cjson = { url = "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz", archive = "tar.gz", strip_components = "1", build = "cmake", cmake_args = "CMAKE_POLICY_VERSION_MINIMUM=3.5;BUILD_SHARED_LIBS=OFF;CJSON_BUILD_SHARED_LIBS=OFF;CJSON_BUILD_STATIC_LIBS=ON" }

[tool.mlang]
static_deps = true
```

`static_deps = true` tells `mlang pkg build` to link fetched package
dependencies via their `.a` archive files instead of using dynamic lookup via
`-l...`.

## Run

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
./build/static_cjson_demo
../../build/mlang pkg clean
```

Or run:

```sh
./run_demo.sh
```

## Notes

- This example focuses on static linking of fetched package dependencies.
- Optional Linux-only C++ runtime flags can be added with:

```toml
[tool.mlang]
static_deps = true
static_cpp_runtime = true
```

- `static_cpp_runtime = true` expands to `-static-libstdc++ -static-libgcc`.
  That is typically useful on GNU/Linux toolchains and is not generally
  supported for fully static executables on macOS.
