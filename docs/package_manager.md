# MLang Package Manager {#package_manager}

This page documents the current `mlang pkg` workflow and the manifest keys used
by package builds.

## Overview

`mlang pkg` is the package-manager entrypoint exposed by the main compiler
frontend:

```sh
./build/mlang pkg <subcommand>
```

By default, `mlang pkg` prefers the MLang implementation in
`tools/mlang-pkg-mla/main.mla` and falls back to the C++ implementation when
needed.

You can force backend selection with:

```sh
MLANG_PKG_IMPL=mla ./build/mlang pkg build
MLANG_PKG_IMPL=cpp ./build/mlang pkg build
```

## Subcommands

### `pkg init`

Create a new `mlang.toml` manifest in the current directory:

```sh
./build/mlang pkg init
```

The generated manifest contains:

```toml
[package]
name = "app"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]

[c-dependencies]
```

### `pkg add`

Add a dependency entry to `mlang.toml`.

Git dependency:

```sh
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git
```

Optional pinning:

```sh
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --tag v1.7.18
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --rev <commit>
```

System / pkg-config dependency:

```sh
./build/mlang pkg add curl --pkg-config libcurl
./build/mlang pkg add zlib --system
```

### `pkg fetch`

Clone or update git dependencies into the package-local `build/deps/` tree:

```sh
./build/mlang pkg fetch
```

For example, a dependency named `cjson` is fetched into:

```text
build/deps/cjson
```

### `pkg build`

Build the package entry defined by `mlang.toml`:

```sh
./build/mlang pkg build
./build/mlang pkg build -O3
./build/mlang pkg build --ninja
```

Current CLI options:

- `-O0`
- `-O1`
- `-O2`
- `-O3`
- `--ninja`

The output binary or IR file is written under the package-local `build/`
directory.

### `pkg run`

Run a custom package task declared with `[[task]]`:

```sh
./build/mlang pkg run kernel-build
```

### `pkg clean`

Remove the package-local artifact tree created by `fetch` and `build`:

```sh
./build/mlang pkg clean
```

This removes:

```text
build/
```

## Manifest Layout

Current package manifests use `mlang.toml`.

Basic example:

```toml
[package]
name = "curl_demo"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]

[c-dependencies]
curl = { pkg_config = "libcurl" }

[tool.mlang]
module_paths = ["."]
opt_level = "O2"
target_arch = "x64"
lib_paths = ["vendor/lib"]
libs = ["foo"]
linker_flags = ["-Wl,-rpath,vendor/lib"]
compiler_flags = ["-Wno-unwrap"]
```

## Manifest Sections

### `[package]`

Supported keys currently used by `mlang pkg`:

- `name`
- `version`
- `entry`

`entry` defaults to `src/main.mla` when omitted.

### `[dependencies]`

Git-backed dependencies are declared as inline TOML tables.

Example:

```toml
[dependencies]
cjson = { git = "https://github.com/DaveGamble/cJSON.git", build = "cmake", cmake_args = "BUILD_SHARED_LIBS=OFF;CJSON_BUILD_STATIC_LIBS=ON" }
```

Currently supported dependency keys:

- `git`
- `url`
- `archive`
- `rev`
- `tag`
- `build`
- `cmake_args`
- `subdir`
- `strip_components`

Current supported build systems:

- `cmake`
- `meson`
- `make`

If `build` is omitted, `cmake` is used.

If `build = "none"` is set, the dependency is fetched but skipped by the
built-in dependency builders during `pkg build`.

Archive source example:

```toml
[dependencies]
cjson = { url = "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz", archive = "tar.gz", strip_components = "1", build = "cmake" }
```

Current supported archive source type:

- `tar.gz`

### `[c-dependencies]`

System libraries are resolved through `pkg-config`.

Examples:

```toml
[c-dependencies]
curl = { pkg_config = "libcurl" }
zlib = { system = true }
```

### `[tool.mlang]`

This section provides package-build defaults for `mlang pkg build`.

Supported keys:

- `module_paths`
- `min_mlang_version`
- `opt_level`
- `target_arch`
- `path_entries` / `bin_paths`
- `use_ninja` / `ninja`
- `lib_paths`
- `libs`
- `linker_flags`
- `compiler_flags`
- `static_deps`
- `static_cpp_runtime`

Key details:

- `opt_level` accepts `O0`, `O1`, `O2`, `O3`, with or without a leading `-`.
- `min_mlang_version` requires the running `mlang` version to be greater than
  or equal to the declared value before `pkg build` proceeds.
- `target_arch` accepts `x86`, `x86-64`, `x64`, `x86_64`, `amd64`,
  `aarch64`, and `arm64`.
- `path_entries` prepends directories to `PATH` for dependency fetch/build
  commands, `pkg-config`, Ninja detection, final package linking, and `pkg run`
  tasks. `bin_paths` is accepted as an alias.
- `use_ninja` requests Ninja for dependency builds and verifies that `ninja`
  or `ninja-build` exists in `PATH` before dependency builds begin.
- `lib_paths` are forwarded as `-L...`.
- `libs` are forwarded as `-l...`.
- `linker_flags` are forwarded as raw flags during package builds.
- `compiler_flags` are forwarded as raw compiler flags during package builds.
- `static_deps` links fetched package dependencies via discovered `.a`
  archives instead of dynamic `-l...` resolution.
- `static_cpp_runtime` adds `-static-libstdc++ -static-libgcc` during package
  linking. This is generally intended for GNU/Linux toolchains.
- libraries declared in `libs` are validated before the real package link step
  so missing `-l...` entries fail early with a clearer error

### `[[bin]]`

Packages can declare multiple executable targets with `[[bin]]`:

```toml
[[bin]]
name = "hello"
entry = "src/hello.mla"

[[bin]]
name = "inspect"
entry = "src/inspect.mla"
opt_level = "O0"
linker_flags = ["-Wl,-dead_strip"]
```

When `[[bin]]` targets are present, `mlang pkg build` builds each one into
`build/<bin-name>`.

Supported target-scoped keys inside `[[bin]]`:

- `min_mlang_version`
- `opt_level`
- `target_arch`
- `path_entries`
- `use_ninja`
- `compiler_flags`
- `linker_flags`
- `lib_paths`
- `libs`
- `static_deps`
- `static_cpp_runtime`

Merge behavior:

- scalar values override package defaults from `[tool.mlang]`
- list values append after package defaults
- explicit target booleans override package defaults

### `[[task]]`

Packages can define shell-driven tasks:

```toml
[[task]]
name = "kernel-build"
workdir = "{{root}}"
commands = [
  "make -C {{deps_dir}}/linux ARCH=arm64 defconfig",
  "make -C {{deps_dir}}/linux ARCH=arm64 -j${JOBS:-4} Image"
]
```

Run them with `./build/mlang pkg run <task>`.

Linux AArch64 kernel example sequence:

```sh
cd examples/package_manager_linux_aarch64_qemu
../../build/mlang pkg fetch
../../build/mlang pkg run kernel-build
../../build/mlang pkg run initramfs
../../build/mlang pkg run qemu-run
```

Supported task keys:

- `name`
- `workdir`
- `command`
- `commands`

Supported placeholders in `workdir` and commands:

- `{{root}}`
- `{{manifest}}`
- `{{build_dir}}`
- `{{deps_dir}}`

An explicit CLI optimization flag such as `-O3` overrides
`[tool.mlang].opt_level`.

### `[workspace]`

Workspace roots can declare recursive package discovery under selected
subdirectories:

```toml
[workspace]
members = ["packages"]
```

Each listed member is resolved relative to the root manifest. If the member is
a directory, `mlang pkg fetch`, `mlang pkg build`, and `mlang pkg clean`
recursively scan for child `mlang.toml` files beneath it.

## Example Workflow

```sh
./build/mlang pkg init
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git
./build/mlang pkg add curl --pkg-config libcurl
./build/mlang pkg fetch
./build/mlang pkg build
./build/mlang pkg clean
```

Workspace example in this repository:

- `examples/package_manager_workspace_fetch`
  Demonstrates recursive workspace package discovery plus GitHub `git` and
  `tar.gz` dependency fetching.
- `examples/package_manager_static_cjson`
  Demonstrates static linking of a fetched `tar.gz` C dependency.
- `examples/package_manager_multi_bins`
  Demonstrates `[[bin]]` targets, target-scoped config overrides, and mixed
  GitHub `git` plus `tar.gz` source dependencies in one package.
- `examples/package_manager_linux_aarch64_qemu`
  Demonstrates a fetch-only Linux kernel dependency plus `[[task]]` commands
  for AArch64 kernel build and QEMU boot flow.

## See Also

- [MLang Documentation Main Page](README.md)
- [Quick Guide](quick_guide.md)
- [Language Syntax](language_syntax.md)
- root project `README.md`
- `examples/package_manager_git_cjson/README.md`
