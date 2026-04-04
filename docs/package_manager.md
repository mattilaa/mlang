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

Create a new `mlang.toml` manifest in the current directory. `pkg init` also
creates `src/main.mla` so the package can build immediately:

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

Generated entry source:

```mla
fn main() {
    println!("app subproject scaffold ready");
}
```

### `pkg add`

Add a dependency entry to `mlang.toml`.

Git dependency:

```sh
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git
```

Generate a complete subproject package automatically:

```sh
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --add-lib
./build/mlang pkg add zlib --url https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz --archive tar.gz --add-lib
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

`--add-lib` creates a subproject under `packages/<name>/`, adds the dependency
to that generated subproject manifest, creates `src/main.mla`, and ensures the
root manifest contains `[workspace] members = ["packages"]`.

Override the generated subproject location with:

```sh
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --add-lib --project-dir packages/json/cjson_demo
```

### `pkg fetch`

Clone or update git dependencies into the configured dependency cache
directory. By default this is `build/deps/`:

If you do not set `build_dir`, `deps_dir`, or any CLI overrides, `pkg fetch`
uses the legacy default layout and stores dependencies under `build/deps/`.

```sh
./build/mlang pkg fetch
./build/mlang pkg fetch --deps-dir .pkg/deps
./build/mlang pkg fetch --build-dir build-release
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
./build/mlang pkg build --build-dir build-release --deps-dir .pkg/deps
```

Current CLI options:

- `-O0`
- `-O1`
- `-O2`
- `-O3`
- `--ninja`
- `--build-dir DIR`
- `--deps-dir DIR`

The output binary or IR file is written under the configured build directory.
By default this is `build/`.

If you do not set `build_dir`, `deps_dir`, or any CLI overrides, `pkg build`
keeps the legacy default layout: binaries go to `build/` and dependencies are
read from `build/deps/`.

### `pkg run`

Run a custom package task declared with `[[task]]`:

```sh
./build/mlang pkg run kernel-build
```

Tasks can model a small workflow graph with:

- `depends_on`
- `next`
- `join_on`
- `phase`
- `next_phases`
- `phase_join_on`
- `parallel`
- `command` / `commands`
- `shell` / `script`

### `pkg clean`

Remove the package-local artifact tree created by `fetch` and `build`:

```sh
./build/mlang pkg clean
./build/mlang pkg clean --build-dir build-release
./build/mlang pkg clean --deps
```

By default this removes the configured build directory:

```text
build/
```

If `deps_dir` points outside the build directory, fetched dependencies are left
in place so they can be reused across `build-debug`, `build-release`, or other
output directories. Pass `--deps` to remove that separate dependency cache too.

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
build_dir = "build-release"
deps_dir = ".pkg/deps"
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
- `build_dir`
- `deps_dir`
- `path_entries` / `bin_paths`
- `make_program`
- `use_ninja` / `ninja`
- `lib_paths`
- `libs`
- `linker_flags`
- `compiler_flags`
- `static_deps`
- `static_cpp_runtime`

Directory behavior:

- `build_dir` controls where `pkg build` writes final binaries and where
  `pkg run` stores generated task scripts. The default is `build`.
- `deps_dir` controls where `pkg fetch` stores fetched sources and where
  `pkg build` looks for built dependency artifacts. The default is
  `<build_dir>/deps`.
- Both paths are resolved relative to the package root unless they are already
  absolute.
- If neither key is set, the default layout stays unchanged: build outputs go
  to `build/` and fetched dependencies live under `build/deps/`.

Example layout with separate outputs and a shared dependency cache:

```toml
[tool.mlang]
build_dir = "build-debug"
deps_dir = ".pkg/deps"
```

```sh
./build/mlang pkg fetch
./build/mlang pkg build
./build/mlang pkg build --build-dir build-release
```

This produces:

```text
.pkg/deps/            # fetched sources and built dependency artifacts
build-debug/app       # default package binary
build-release/app     # alternate output directory from CLI override
```

### `[[task]]`

Custom tasks are executed with `mlang pkg run <task-name>`.

Supported task keys:

- `name`
- `phase`
- `workdir`
- `parallel`
- `depends_on`
- `phase_depends_on`
- `next`
- `next_phases`
- `join_on`
- `phase_join_on`
- `env`
- `command`
- `commands`
- `shell` / `script`

Task semantics:

- `depends_on` runs prerequisite tasks before the current task.
- `phase_depends_on` waits for every task tagged with a named phase.
- `next` launches named downstream tasks after the current task succeeds.
- `next_phases` launches every task tagged with a named phase.
- `join_on` waits for named tasks before the current task runs.
- `phase_join_on` waits for every task tagged with a named phase.
- `parallel = true` allows multiple `depends_on` or `next` branches to run
  concurrently.
- `shell = [ ... ]` writes an inline shell script under `build/task-scripts/`
  and runs it through `sh`.

Host-specific overrides are supported with subtables such as:

```toml
[task.host.darwin]
shell = [
  "docker build -t my-image {{root}}"
]
```

Example workflow graph:

```toml
[[task]]
name = "workflow"
parallel = true
next = ["left", "right", "merge"]
commands = [
  "mkdir -p {{build_dir}}"
]

[[task]]
name = "left"
commands = [
  "sh -c 'echo left > {{build_dir}}/left.txt'"
]

[[task]]
name = "right"
commands = [
  "sh -c 'echo right > {{build_dir}}/right.txt'"
]

[[task]]
name = "merge"
join_on = ["left", "right"]
commands = [
  "sh -c 'cat {{build_dir}}/left.txt {{build_dir}}/right.txt > {{build_dir}}/joined.txt'"
]
```

Running:

```sh
./build/mlang pkg run workflow
```

starts `left`, `right`, and `merge`. The `merge` task waits until both branch
tasks are complete because of `join_on`.

Phase example:

```toml
[[task]]
name = "phase-workflow"
parallel = true
next = ["phase-link"]
next_phases = ["compile"]

[[task]]
name = "compile-left"
phase = "compile"
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c 'echo phase-left > {{build_dir}}/phase-left.txt'"
]

[[task]]
name = "compile-right"
phase = "compile"
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c 'echo phase-right > {{build_dir}}/phase-right.txt'"
]

[[task]]
name = "phase-link"
phase_join_on = ["compile"]
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c 'cat {{build_dir}}/phase-left.txt {{build_dir}}/phase-right.txt > {{build_dir}}/phase-joined.txt'"
]
```

Key details:

- `opt_level` accepts `O0`, `O1`, `O2`, `O3`, with or without a leading `-`.
- `min_mlang_version` requires the running `mlang` version to be greater than
  or equal to the declared value before `pkg build` proceeds.
- `target_arch` accepts `x86`, `x86-64`, `x64`, `x86_64`, `amd64`,
  `aarch64`, and `arm64`.
- `path_entries` prepends directories to `PATH` for dependency fetch/build
  commands, `pkg-config`, Ninja detection, final package linking, and `pkg run`
  tasks. `bin_paths` is accepted as an alias.
- `make_program` selects the make executable used for built-in
  `build = "make"` dependency builds, and is exposed to `[[task]]` commands as
  the `{{make}}` placeholder.
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
`<build_dir>/<bin-name>`.

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
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} defconfig",
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} -j${JOBS:-4} Image"
]

[task.host.darwin]
depends_on = ["darwin-native-prepare"]
env = [
  "HOSTCFLAGS=-Iscripts/macos-include -I/opt/homebrew/opt/libelf/include -I/usr/local/opt/libelf/include -Wno-error=incompatible-pointer-types",
  "HOSTLDFLAGS=-L/opt/homebrew/opt/libelf/lib -L/usr/local/opt/libelf/lib",
  "HOSTLDLIBS=-lelf"
]
commands = [
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 LLVM=1 LLVM_IAS=1 defconfig",
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 LLVM=1 LLVM_IAS=1 -j${JOBS:-4} Image"
]

[tool.mlang]
make_program = "gmake"
```

Run them with `./build/mlang pkg run <task>`.

Linux AArch64 kernel example sequence:

```sh
cd examples/package_manager_linux_aarch64_qemu
../../build/mlang pkg fetch
../../build/mlang pkg run boot-flow
```

Linux AArch64 kernel example installation on macOS:

```sh
brew install llvm lld libelf gnu-sed make qemu cpio
```

Linux AArch64 kernel example installation on Debian/Ubuntu:

```sh
sudo apt-get install clang lld make qemu-system-arm cpio gzip gcc-aarch64-linux-gnu
```

Supported task keys:

- `name`
- `workdir`
- `parallel`
- `depends_on`
- `next`
- `join_on`
- `env`
- `shell`
- `command`
- `commands`

Host-conditional task overrides:

- `[task.host.darwin]`
- `[task.host.linux]`
- `[task.host.windows]`

When a host override exists for the current host:

- override `workdir` replaces the base `workdir`
- override `parallel` replaces the base `parallel`
- override `depends_on` appends after the base `depends_on`
- override `join_on` appends after the base `join_on`
- override `next` appends after the base `next`
- override `env` appends after the base `env`
- override `shell` replaces the base inline shell script
- override `command` / `commands` replace the base task commands

Supported placeholders in `workdir` and commands:

- `{{root}}`
- `{{manifest}}`
- `{{build_dir}}`
- `{{deps_dir}}`
- `{{make}}`

`{{build_dir}}` and `{{deps_dir}}` expand to the configured `[tool.mlang]`
directories for that package.

The Linux kernel example uses:

- `toolchain-check` to fail early if required LLVM, GNU `sed`, or `libelf`
  pieces are missing
- `darwin-native-prepare` to generate compatibility headers and patch
  `scripts/mod/file2alias.c` for native Apple Silicon builds
- `kernel-build` to run `defconfig` and build `arch/arm64/boot/Image`
- `initramfs` to build the example initramfs archive
- `qemu-run` to boot the generated image under QEMU after its prerequisites
  complete, optionally in parallel

Host guidance:

- Linux is the recommended host for this example.
- Apple Silicon macOS is supported through a native path based on
  <https://seiya.me/blog/building-linux-on-macos-natively>.
- That Darwin path expects Homebrew `llvm`, `lld`, `libelf`, `gnu-sed`,
  `make`, `qemu`, and `cpio`.
- On macOS, the example tasks enable the kernel's LLVM toolchain mode, add
  Homebrew `libelf` include/library flags, generate `elf.h` and `byteswap.h`
  compatibility headers, patch `scripts/mod/file2alias.c`, and relax
  `-Wincompatible-pointer-types` for host tools to get past known Homebrew
  `libelf` typedef mismatches in `scripts/sorttable`.
- Linux remains the recommended host for reproducible full-kernel builds, but
  the example now carries a native Apple Silicon path instead of requiring
  Docker.

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

## Workspace And Subdirectory Layouts

This topic shows how to organize a workspace root, nested package manifests,
and fetched source subprojects.

### Basic Workspace Tree

```text
my_workspace/
├── mlang.toml
└── packages/
    ├── cli_app/
    │   ├── mlang.toml
    │   └── src/
    │       └── main.mla
    └── json_tool/
        ├── mlang.toml
        └── src/
            └── main.mla
```

Workspace root manifest:

```toml
[workspace]
members = ["packages"]
```

Subpackage manifest:

```toml
[package]
name = "cli_app"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]

[c-dependencies]

[tool.mlang]
opt_level = "O2"
```

Run from the workspace root:

```sh
./build/mlang pkg fetch
./build/mlang pkg build
./build/mlang pkg clean
```

### Workspace With Fetched Subprojects

Each discovered subpackage can fetch its own sources from GitHub or from a
release archive URL.

```text
workspace_fetch/
├── mlang.toml
└── packages/
    ├── git_cjson_demo/
    │   ├── mlang.toml
    │   └── src/
    │       └── main.mla
    └── tarball_cjson_demo/
        ├── mlang.toml
        └── src/
            └── main.mla
```

Workspace root:

```toml
[workspace]
members = ["packages"]
```

Git-backed subpackage:

```toml
[package]
name = "git_cjson_demo"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]
cjson = { git = "https://github.com/DaveGamble/cJSON.git", tag = "v1.7.18", build = "cmake" }

[c-dependencies]
```

Tarball-backed subpackage:

```toml
[package]
name = "tarball_cjson_demo"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]
cjson = { url = "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz", archive = "tar.gz", strip_components = "1", build = "cmake" }

[c-dependencies]
```

Run from the workspace root:

```sh
./build/mlang pkg fetch
./build/mlang pkg build
```

See `examples/package_manager_workspace_fetch` for a complete working example.

### Generate A Subproject With `pkg add --add-lib`

You can generate the subdirectory tree instead of creating it manually.

Start from a root manifest:

```toml
[package]
name = "workspace_root"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]

[c-dependencies]
```

Run:

```sh
./build/mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --add-lib
```

Generated layout:

```text
.
├── mlang.toml
└── packages/
    └── cjson/
        ├── mlang.toml
        └── src/
            └── main.mla
```

Generated root manifest fragment:

```toml
[workspace]
members = ["packages"]
```

Generated subproject manifest:

```toml
[package]
name = "cjson_demo"
version = "0.1.0"
entry = "src/main.mla"

[dependencies]
cjson = { git = "https://github.com/DaveGamble/cJSON.git" }

[c-dependencies]
```

Then build from the root:

```sh
./build/mlang pkg fetch
./build/mlang pkg build
```

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
