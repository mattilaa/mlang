# mlang
MLang - Programming Language

For more documentation, visit the [MLang GitHub Wiki](https://github.com/mattilaa/mlang/wiki).

## Table Of Contents
- [What Is Mlang](#what-is-mlang)
- [Compile-Time Evaluation With `cexpr`](#compile-time-evaluation-with-cexpr)
- [Fixed-Capacity Arrays](#fixed-capacity-arrays)
- [Tools Shipped In This Repository](#tools-shipped-in-this-repository)
- [Build From Scratch (Step By Step)](#build-from-scratch-step-by-step)
- [Scripted Bootstrap And Build](#scripted-bootstrap-and-build)
- [LSP](#lsp)
- [C++ LSP](#c-lsp)
- [Mlangd (Mlang Scaffold)](#mlangd-mlang-scaffold)
- [Compiler Frontend (Primary MLang CLI)](#compiler-frontend-primary-mlang-cli)
- [Package Manager (MLang Backend Default)](#package-manager-mlang-backend-default)
- [Stdlib Linking](#stdlib-linking)
- [Build + Install](#build--install)
- [Documentation](#documentation)
- [AddressSanitizer Verification](#addresssanitizer-verification)
- [Codesigning Built Binaries On macOS](#codesigning-built-binaries-on-macos)
- [Formatter](#formatter)
- [Quickstart (Package Manager + curl example)](#quickstart-package-manager--curl-example)
- [Testing](#testing)
- [Multithreaded TCP Demo (Local)](#multithreaded-tcp-demo-local)
- [Advanced Protocol Stack Demo (Local)](#advanced-protocol-stack-demo-local)
- [Examples](#examples)
- [Object-Oriented Language Features](#object-oriented-language-features)
- [Package Manager (C++)](#package-manager-c)
- [Package Workspaces And Fetched Subprojects](#package-workspaces-and-fetched-subprojects)

## What Is Mlang
`mlang` is the compiler and primary CLI for the MLang programming language.
This repository also contains the standard library, the package manager,
frontend tooling, formatter, and language-server related tools built around
the same toolchain.

The toolchain is **self-hosted in stages**: a small C++ "seed compiler"
(`mlang`) and its static stdlib (`mlang_std`) are built first via CMake.
Every other tool in `tools/` is then itself an MLang program — those
binaries are produced by feeding their `.mla` source through the seed
compiler. The bootstrap launcher in `bootstrap/` is a thin wrapper around
`mlang pkg` that orchestrates this in the right order.

## Compile-Time Evaluation With `cexpr`

MLang supports explicit compile-time evaluation with `cexpr(expr)` and
`cexpr fn`.

```mla
cexpr fn twice(x: i32) -> i32 {
    return x * 2;
}

fn main() -> i32 {
    static_assert!(twice(21) == 42);
    let value: i32 = cexpr(twice(21));
    return value == 42 ? 0 : 1;
}
```

The equivalent postfix declaration form is also accepted:

```mla
fn twice(x: i32) cexpr -> i32 {
    return x * 2;
}
```

Floating-point compile-time functions are supported in the same first-version
subset:

```mla
cexpr fn midpoint(a: f32, b: f32) -> f32 {
    return (a + b) / 2.0f;
}
```

Compile-time values use the same keyword:

```mla
cexpr Steps: i32 = 21;

fn main() -> i32 {
    cexpr Local: i32 = Steps * 2;
    return cexpr(Local);
}
```

Compile-time branches use `cexpr if`:

```mla
cexpr UseFastPath: bool = true;

fn main() -> i32 {
    cexpr if UseFastPath {
        return 0;
    } else if false {
        return missing_other_symbol();
    } else {
        return missing_runtime_symbol();
    }
}
```

Compile-time functions can also be generic and dispatch on concrete argument
types with `type_id(T)`:

```mla
alias SomeType = i64;

struct Marker {
    var value: i32;
};

generic<T, Y>
cexpr fn pick(item: T, item2: Y) {
    cexpr if type_id(T) == SomeType {
        return item * 2;
    } else if type_id(T) == Marker {
        return 7;
    } else {
        return item2;
    }
}
```

Current first-version scope:
- `cexpr(...)` folds integer, floating-point, and `bool` expressions during compilation.
- `cexpr fn` marks functions that may be called from compile-time contexts.
- `generic<T, ...> cexpr fn` supports one or more type parameters for compile-time calls.
- Struct arguments can participate in `generic<T> cexpr fn` type dispatch via
  `type_id(T)`, but struct fields and struct constants are not compile-time
  evaluable yet.
- `cexpr name: Type = expr;` declares a compile-time value.
- `cexpr if` selects a branch during compilation.
- Calling a normal runtime `fn` from `cexpr(...)` is rejected.

## Fixed-Capacity Arrays

`array<T, N>` is a fixed-capacity, list-compatible sequence type. Literal and
fill initializers are checked at compile time, so the compiler rejects
initializers with more than `N` elements.

```mla
let fixed: array<i32, 6> = {1, 3, 4, 5, 6, 7};
var scratch: array<i32, 6> = {};
scratch.push(10);
scratch.push(20);
scratch.extend(vec![30, 40]);
scratch.pop();
scratch.fill(1); // fills all 6 slots
```

## Tools Shipped In This Repository

| Binary                | Source                              | Built By           | What It Does                                                                                       | Why It Exists                                                                                                                           |
| --------------------- | ----------------------------------- | ------------------ | -------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `mlang`               | `src/main.cpp` + `src/ir.cpp` (C++) | CMake              | Compiler driver: lex → parse → IR → LLVM → object/exec. Also routes `mlang test`, `mlang pkg`, etc.| The seed compiler. Everything else is built by feeding `.mla` source through this. Without it nothing else can be produced.             |
| `libmlang_std.a`      | `stdlib/src/*.{c,cpp}` + `stdlib/std/*.mla` (C/C++ runtime) | CMake | Static library implementing the `std::*` modules — testing, fs, json, time, threads, etc.          | The runtime every MLang program links against (`-lmlang_std`). Built once by CMake; consumed by every binary the bootstrap produces.    |
| `mlang-frontend-mla`  | `tools/mlang-frontend-mla/main.mla` | bootstrap          | Higher-level CLI that wraps `mlang`: directory test/bench mode, stdlib auto-linking, pkg routing.  | Demonstrates self-hosting: the user-facing CLI written in MLang. Routed via `MLANG_FRONTEND_IMPL=mla` or installed as `mlang-frontend`. |
| `mlang-frontend`      | shell launcher generated by bootstrap | bootstrap        | Thin shim that execs `mlang-frontend-mla` with the seed compiler as backend.                       | Lets users run the MLang-implemented frontend without typing `--backend mlang` every time.                                              |
| `mlang-format`        | `tools/mlang-format-mla/main.mla`   | bootstrap          | Source formatter (reads `.mlang-format`, applies via stdin/stdout or in-place).                    | Code style enforcement. Used standalone and by `mlangd-mla` to satisfy LSP `textDocument/formatting`.                                   |
| `mlangd-mla`          | `tools/mlangd-mla/main.mla`         | bootstrap          | MLang language server (LSP-over-stdio): diagnostics, hover, definition, references, formatting.   | Editor integration. Drives features for VS Code, UVim, Helix, etc. via JSON-RPC.                                                        |
| `mlangpkg` / `mlang-pkg-mla` | `tools/mlang-pkg-mla/main.mla` (MLang) **and** `src/package_manager.cpp` (C++) | seed compiler **or** bootstrap | Package manager: `init`, `add`, `fetch`, `build`, `clean`, plus a task runner for tasks declared in `mlang.toml`. | Drives dependency fetching, builds, and the bootstrap itself. The MLang implementation is preferred (`MLANG_PKG_IMPL=mla`); the C++ backend handles task trees and CMake/Ninja orchestration. Reachable as `mlang pkg ...`. |
| `bootstrap/run-bootstrap.sh` | shell wrapper                | (no compile)       | Optional convenience shim that forwards to `mlang pkg --config bootstrap/mlang.toml run <task>`. | Pre-existing helper for users who prefer a shell entry point. Everything in this README uses `mlang pkg` directly so the build steps work the same way once `mlang` is installed. |

A clean checkout produces these binaries under `./build/`:

```
build/mlang                  ← seed compiler (CMake)
build/libmlang_std.a         ← runtime stdlib (CMake)
build/mlangd-mla             ← LSP server          (bootstrap, depends on seed)
build/mlang-frontend-mla     ← MLang frontend CLI  (bootstrap, depends on seed)
build/mlang-frontend         ← bash shim → mlang-frontend-mla
build/mlang-format           ← formatter           (bootstrap, depends on seed)
```

## Build From Scratch (Step By Step)

### 0. Install host dependencies

| Need        | Why                                                              |
| ----------- | ---------------------------------------------------------------- |
| `cmake`     | Build system for the seed compiler and stdlib                    |
| LLVM 15+    | Backend for the seed compiler (uses `libLLVMSupport`, `libLLVMCore`, codegen targets) |
| `flex`      | Generates `lexer.cpp` from `src/lexer.l`                         |
| `bison`     | Generates `parser.cpp/hpp` from `src/parser.y`                   |
| OpenSSL     | Required by `std::ssl` and TLS-using parts of the stdlib         |
| `python3`   | Used by `scripts/generate_ast_bridge.py` and the docs runner     |
| `c++` toolchain | Links the artifacts produced by the seed compiler            |
| `doxygen`   | Required only if you build the docs (`run docs`)                 |

macOS (Homebrew):
```sh
brew install cmake llvm flex bison openssl@3 python doxygen
```

Debian/Ubuntu:
```sh
sudo apt install cmake llvm-dev libllvm15 clang flex bison libssl-dev python3 build-essential doxygen graphviz
```

## Scripted Bootstrap And Build

The root scripts mirror the `uvim` workflow and keep the full build explicit:
first build the C++ seed compiler/runtime and `mlang-config`, then compile the
MLang-native tools from their `.mla` sources with that freshly built compiler.
There is no prebuilt compiler or hidden bootstrap binary.

POSIX/macOS:
```sh
./bootstrap.sh
./build.sh --install
```

Windows PowerShell:
```powershell
./bootstrap.ps1
./build.ps1 --install
```

What the scripts do:
- `bootstrap.sh` / `bootstrap.ps1` run CMake and build `mlang`,
  `mlang_std`, and `mlang-config`.
- `mlang-config` writes `build/mlang-config.conf` and
  `build/mlang_config_cache.cmake`.
- `build.sh` / `build.ps1` reconfigure from that cache, rebuild the seed
  compiler/runtime, then explicitly compile:
  `tools/mlangd-mla/main.mla`, `tools/mlang-format-mla/main.mla`,
  `tools/mlang-frontend-mla/main.mla`, and `tools/mlangpkg/mlangpkg.mla`.
- Install prefixes such as `~/.local` are expanded before they are written to
  the CMake cache, so direct installs such as
  `cmake --install build --config Release --prefix ~/.local` use an absolute
  destination instead of a literal `~` directory.
- When `bootstrap.sh` / `bootstrap.ps1` runs `mlang-config`, it also asks
  whether to build and install the output binaries to the configured install
  location.

Useful non-interactive forms:
```sh
./bootstrap.sh --build-dir build -j 8
./build/mlang-config --build-dir build --install-prefix ~/.local --bin-dir ~/.local/bin --unit-tests off --robot-tests off --write
./build.sh --build-dir build --install
```

```powershell
./bootstrap.ps1 --build-dir build -j 8
./build/mlang-config.exe --build-dir build --install-prefix "$HOME/.local" --bin-dir "$HOME/.local/bin" --unit-tests off --robot-tests off --write
./build.ps1 --build-dir build --install
```

The lower-level manual commands below are the same stages written out by hand.

### 1. Build the seed compiler and stdlib (CMake)

This produces `./build/mlang` and `./build/libmlang_std.a`. Nothing else
in the toolchain can run without these.

```sh
cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mlang mlang_std -j 8
```

Verify:
```sh
./build/mlang --version
```

### 2. Put `mlang` on PATH (or pin it explicitly)

The bootstrap launcher needs to find a working `mlang`. Pick one:

**Option A — install to `~/.local/bin`:**
```sh
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
mlang --version
```

**Option B — point the bootstrap at the build tree:**
```sh
export MLANG_BOOTSTRAP_BIN="$(pwd)/build/mlang"
```

### 3. Build the MLang-native tools

Now that `mlang` (and therefore `mlang pkg`) is available, drive every
remaining tool through the bootstrap manifest at `bootstrap/mlang.toml`.
The package manager resolves the task graph and compiles each MLang
source through the seed compiler in the right order.

```sh
mlang pkg --config bootstrap/mlang.toml run build-all
```

It produces, under `./build/`:
- `mlangd-mla` — LSP server (depends on `mlang_std`)
- `mlang-frontend-mla` — MLang-implemented frontend CLI
- `mlang-frontend` — shell shim that execs `mlang-frontend-mla`
- `mlang-format` — formatter

To preview what `build-all` will do (lists tasks, no execution):
```sh
mlang pkg --config bootstrap/mlang.toml run build-all --tasks
```

To build any one tool independently:
```sh
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla
mlang pkg --config bootstrap/mlang.toml run build-mlang-format
mlang pkg --config bootstrap/mlang.toml run build-mlang-frontend
```

(All three depend on `build-mlang`, which `build-all` already covers.)

### 4. Run the test suite (optional, recommended)

```sh
mlang pkg --config bootstrap/mlang.toml run unit-tests
mlang pkg --config bootstrap/mlang.toml run robot-tests
```

Or directly:
```sh
./build/mlang --tests tests/
./build/mlang --tests tests/ --filter "addition"
```

### 5. Build the documentation (optional)

Runs `doxygen docs/Doxyfile` and writes HTML to `docs/out/html/index.html`.
Requires `doxygen` (and `graphviz` for diagrams on Linux) from step 0.

```sh
mlang pkg --config bootstrap/mlang.toml run docs
open docs/out/html/index.html   # macOS; use xdg-open on Linux
```

### 5b. Build the man pages (optional)

The POSIX man pages are checked into the repository as roff sources under
`docs/man/`. There is no extra generator step.

Stage them into the build tree:

```sh
cmake --build build --target manpages
find build/man/man1 -maxdepth 1 -type f | sort
```

Preview one directly from the source tree or staged build output:

```sh
man -l docs/man/mlang.1
man -l build/man/man1/mlang-format.1
```

Install them together with the tools:
- `cmake --install build --prefix "$HOME/.local"` installs the native pages
  for `mlang`, `mlang-pkg`, `mlangd`, and `mlang-format`
- `mlang pkg --config bootstrap/mlang.toml run install-all ...` also installs
  the bootstrap-managed pages for `mlangd-mla`, `mlang-frontend`, and
  `mlang-frontend-mla`

### 6. Install everything

This installs `mlangd-mla`, `mlang-frontend`, and `mlang-format` to
`~/.local/bin` (or wherever `install_prefix` points). `mlang` itself was
already installed by `cmake --install` in step 2. Man pages are installed under
`$install_prefix/share/man/man1`, so commands such as `man mlang`,
`man mlang-pkg`, `man mlangd`, `man mlang-format`, `man mlangd-mla`, and
`man mlang-frontend` work after installation.

```sh
mlang pkg --config bootstrap/mlang.toml run install-all
```

To do build + install in one shot (after step 2):
```sh
mlang pkg --config bootstrap/mlang.toml run build-and-install
```

Custom prefix:
```sh
mlang pkg --config bootstrap/mlang.toml run build-and-install \
    --option install_prefix=$HOME/.local \
    --option bin_dir=$HOME/.local/bin
```

After install, verify the full toolchain:
```sh
which mlang mlang-frontend mlang-format mlangd-mla
mlang --version
mlang-frontend --help
mlang-format --help
mlangd-mla --stdio < /dev/null
man mlang
man mlang-format
```

### Build order at a glance

```
step 0:  install host deps                                            (system pkg manager)
step 1:  cmake → mlang + libmlang_std.a                               (CMake)
step 2:  install / export PATH                                        (cmake --install)
step 3:  mlang pkg ... run build-all
            ├── mlangd-mla         (uses build/mlang + build/libmlang_std.a)
            ├── mlang-frontend-mla (uses build/mlang + build/libmlang_std.a)
            ├── mlang-frontend     (shell shim writer)
            └── mlang-format       (uses build/mlang + build/libmlang_std.a)
step 4:  mlang pkg ... run unit-tests / robot-tests                   (optional)
step 5:  mlang pkg ... run docs                                       (optional, doxygen)
step 6:  mlang pkg ... run install-all                                (copies to ~/.local/bin)
```

The bootstrap phases are intentionally split, so you can run only what you
need (each is `mlang pkg --config bootstrap/mlang.toml run <name>`):
- `build-mlang` — just the seed compiler (delegates to CMake; equivalent to step 1)
- `build-mlangd-mla` — just the LSP server
- `build-mlang-format` — just the formatter
- `build-mlang-frontend` — the MLang frontend CLI + shell shim
- `unit-tests` — `mlang --tests tests/`
- `robot-tests` — Robot Framework end-to-end tests
- `docs` — Doxygen + custom doc generators
- `install-mlang`, `install-mlangd-mla`, `install-mlang-format`, `install-mlang-frontend` — install one tool at a time
- `build-and-install` — `build-all` + `install-all` chained together

`mlang pkg --config bootstrap/mlang.toml --help` lists every task with its
description and dependencies. A thin shell convenience wrapper exists at
`bootstrap/run-bootstrap.sh` that forwards to the same `mlang pkg run`
calls; the commands above are what it runs internally.

## LSP
Primary LSP servers:
- `build/mlangd` (C++)
- `build/mlangd-mla` (Mlang)

```sh
mlangd --stdio
```

## C++ LSP
C++ LSP server target is `mlangd` (stdio).

```sh
cmake -S . -B build
cmake --build build
mlangd --stdio
```

Use with UVim:

```sh
uvim --mlang-lsp --mlang-lsp-path /path/to/mlangd
```

UVim `<leader>f` formatting works through `textDocument/formatting`; `mlangd`
now uses `mlang-format` and `.mlang-format` (`--style=file`) for buffer
formatting.

Enable LSP debug telemetry (`cache clears`, `active docs`, `evictions`):

```sh
MLANG_LSP_DEBUG=1 /path/to/mlangd --stdio
```

Write debug output to a file:

```sh
MLANG_LSP_DEBUG=1 MLANG_LSP_DEBUG_LOG=/tmp/mlangd_telemetry.log /path/to/mlangd --stdio
```

Tune semantic cache clear cadence for long-running sessions:

```sh
MLANGD_COMPILER_CACHE_CLEAR_INTERVAL=180 path/to/mlangd --stdio
```

## Mlangd (Mlang Scaffold)
Initial Mlang implementation scaffold lives at `tools/mlangd-mla/main.mla`.
It uses `std::jsonrpc::run_stdio_loop(...)` and a Mlang dispatcher hook:
`__mlang_std_jsonrpc_runtime_dispatch(...)`.

Build object:

```sh
mlang -c tools/mlangd-mla/main.mla -L ./build -lmlang_std
```

Build executable:

```sh
mlang tools/mlangd-mla/main.mla -L ./build -lmlang_std -o /tmp/mlangd-mla
/tmp/mlangd-mla --stdio
```

## Compiler Frontend (Primary MLang CLI)
Feature-rich frontend implementation lives at `tools/mlang-frontend-mla/main.mla`.
Current scope:
- parses frontend option `--backend`
- command-dispatch parity for `test`, `run tests`, `bench`, `pkg`
- directory suite mode for `test`/`bench` (runs suites one-by-one)
- stdlib link auto-discovery for compile passthrough (`-L... -lmlang_std -lm`)
- pkg frontend binary caching (skip recompilation when cached tool is up-to-date)

```sh
mlang tools/mlang-frontend-mla/main.mla -L ./build -lmlang_std -o /tmp/mlang-frontend-mla
/tmp/mlang-frontend-mla --backend mlang --help
/tmp/mlang-frontend-mla --backend mlang examples/main.mla -o /tmp/main_bin
/tmp/mlang-frontend-mla --backend mlang test tests
/tmp/mlang-frontend-mla --backend mlang run tests tests
/tmp/mlang-frontend-mla --backend mlang bench tests --bench-iters 200 --bench-warmup 20
```

You can route `mlang` itself through the MLang frontend implementation:

```sh
MLANG_FRONTEND_IMPL=mla mlang examples/main.mla -o /tmp/main_bin
```

After install, prefer:

```sh
mlang-frontend --help
mlang-frontend examples/main.mla -o /tmp/main_bin
mlang-frontend test tests
```

`mlang` emits `mlang_commands.json` for editor tooling. You can add module
search paths in `mlang.toml`:

```toml
[tool.mlang]
module_paths = ["modules", "vendor/mlang"]
```

## Inline Assembly
MLang supports direct inline assembly through the `asm` keyword. The compiler
lowers it straight to LLVM inline asm, so there is no extra C wrapper boundary.

Supported forms:

```mla
let value: i64 = 9;
let copy: i64 = asm(i64, "", value);
asm volatile(void, "", value);
```

Current first-version constraints:
- operands must be integer or pointer values
- result type must be integer, pointer, or `void`
- non-`void` asm uses the first operand as the tied input/output operand
- `asm volatile(...)` lowers with LLVM `sideeffect`
- plain `asm(...)` lowers without `sideeffect`

Architecture-qualified module assembly is available for freestanding entry
code and definitions that cannot live inside a normal function:

```mla
asm x86(".code16
.globl _start
_start:
    cli
    hlt
");
```

The qualifier must match `--target-arch`. Module assembly has no operands or
result value. See `examples/qemu_x86_bootloader` for a two-stage BIOS loader
and separately linked MLang kernel with an interactive serial terminal, built
through `mlang.toml`. The example mounts a small hierarchical filesystem and
provides `pwd`, `ls`, `cd`, `cat`, `touch`, and a full-screen modal `/bin/vi`
text editor built on `std::esc::freestanding`.
The configurable MFS2 image is writable, and its protected-mode ATA driver
persists file metadata and text contents to the disk image.

## Package Manager (MLang Backend Default)
`mlang pkg ...` now prefers the MLang implementation in
`tools/mlang-pkg-mla/main.mla` by default, with automatic fallback to the C++
backend if needed.

Force backend selection with:

```sh
MLANG_PKG_IMPL=mla mlang pkg init
MLANG_PKG_IMPL=cpp mlang pkg init
```

`mlang pkg init` now scaffolds both `mlang.toml` and `src/main.mla`, so a new
package can be built immediately.

When one project root needs multiple package manifests, use `--config` before
the subcommand to pick the active file for that invocation. This keeps the
default `mlang.toml` behavior intact while allowing per-architecture manifests
such as `arm64.toml` and `x64.toml` in the same directory:

```sh
mlang pkg --config arm64.toml fetch
mlang pkg --config arm64.toml build
mlang pkg --config x64.toml build
mlang pkg --config qemu-aarch64.toml run qemu-run
```

Build and run:

```sh
mlang tools/mlang-pkg-mla/main.mla -L ./build -lmlang_std -o /tmp/mlang-pkg-mla
/tmp/mlang-pkg-mla --backend mlang init
/tmp/mlang-pkg-mla --backend mlang --config arm64.toml build -O2
/tmp/mlang-pkg-mla --backend mlang add cjson --git https://github.com/DaveGamble/cJSON.git
/tmp/mlang-pkg-mla --backend mlang fetch
/tmp/mlang-pkg-mla --backend mlang build -O2
/tmp/mlang-pkg-mla --backend mlang build --build-dir build-release --deps-dir .pkg/deps
/tmp/mlang-pkg-mla --backend mlang build --asan
/tmp/mlang-pkg-mla --backend mlang clean
/tmp/mlang-pkg-mla --backend mlang clean --deps
# Optional for CMake-based deps:
/tmp/mlang-pkg-mla --backend mlang build -O2 --ninja
```

Generate a complete subproject package automatically:

```sh
mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --add-lib
mlang pkg add zlib --url https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz --archive tar.gz --add-lib
```

`mlang pkg clean` removes the configured build directory. When `deps_dir`
points outside it, fetched dependencies are kept for reuse unless you pass
`--deps`.

`mlang pkg build --asan` and `mlang pkg run <task> --asan` force AddressSanitizer
flags onto every binary-producing build step. When `--asan` is used, the
package manager switches those builds to a debug-friendly `-O0` configuration
and warns if an explicit release optimization would otherwise have been used.

Task-driven manifests can also declare runtime-selectable values under
`[tool.mlang.options]`. Override them per invocation with
`mlang pkg run ... --option key=value`, then reference them in task text via
placeholders such as `{{option.userspace}}`:

```toml
[tool.mlang.options]
userspace = "busybox"

[[task]]
name = "qemu-run"
print = "Booting QEMU with {{option.userspace}} userspace"
```

```sh
mlang pkg run qemu-run --option userspace=gnu
```

Custom task workflows can be declared in `mlang.toml` and run with:

```sh
mlang pkg run <task-name>
```

Current task graph features:
- `depends_on` for prerequisites
- `next` for downstream jumps to named tasks
- `join_on` for waiting on named tasks before continuing
- `phase` for grouping tasks into named compile/build phases
- `next_phases` for launching all tasks in a named phase
- `phase_join_on` for waiting until all tasks in a phase complete
- `parallel = true` for concurrent prerequisite/downstream branches
- `inline_output = true` for a single live task status line with the task
  number, spinner, and a truncated tail of the latest output line; the task
  still ends with one final completion line in the form
  `[n/N] task-name Completed, time HH:MM:SS:MS - description`
- `shell = [ ... ]` / `script = [ ... ]` for inline shell scripts stored under `build/task-scripts/`
- `command = [ "binary", "arg1", "arg2" ]` for one readable tokenized command
- `commands = [ [ "binary", "arg1" ] ]` and `commands += [ ... ]` for
  multiline appended command lists
- `chmod = "644"` plus `chmod_path` / `chmod_paths` for recursive permission
  fixups after a task succeeds
- `[tool.mlang.options]` plus `pkg run --option key=value` for manifest-driven
  runtime mode switches such as alternate guest userspaces
- `[task.host.darwin]`, `[task.host.linux]`, `[task.host.windows]` for host-specific overrides

Permission-fixup example:

```toml
[[task]]
name = "extract-src"
commands = [
  [
    "tar",
    "-xzf",
    "{{build_dir}}/archive.tar.gz",
    "-C",
    "{{build_dir}}/src"
  ]
]
chmod = "644"
chmod_paths = [
  "{{build_dir}}/src"
]
```

`chmod` currently accepts octal modes such as `644` or `755`. The mode is
applied recursively to files, and directories keep traverse bits so
`chmod = "644"` still leaves an extracted source tree readable and enterable.

A minimal workflow example:

```toml
[[task]]
name = "workflow"
parallel = true
next = ["left", "right", "merge"]
commands = ["mkdir -p {{build_dir}}"]

[[task]]
name = "left"
commands = ["sh -c 'echo left > {{build_dir}}/left.txt'"]

[[task]]
name = "right"
commands = ["sh -c 'echo right > {{build_dir}}/right.txt'"]

[[task]]
name = "merge"
join_on = ["left", "right"]
commands = ["sh -c 'cat {{build_dir}}/left.txt {{build_dir}}/right.txt > {{build_dir}}/joined.txt'"]
```

Run it from `examples/package_manager_task_graph`:

```sh
mlang pkg run workflow
cat build/joined.txt
```

Command lists can also be written in a more readable tokenized form:

```toml
[[task]]
name = "toolchain-check"
commands = [
  [
    'sh',
    '-c',
    'if ! command -v mlang >/dev/null; then echo Missing mlang.; exit 1; fi',
  ],
]
commands += [
  [
    'sh',
    '-c',
    'for tool in cc c++ ar python3; do if ! command -v $tool >/dev/null 2>&1; then echo Missing required tool in PATH: $tool; exit 1; fi; done',
  ],
]
```

Phase-based barriers are also supported:

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

## Stdlib Linking
When using stdlib modules backed by native code (e.g. `std::math`), link
against the stdlib library just like GCC/Clang:

```sh
mlang main.mla -L ~/.local/lib/mlang -lmlang_std
```

You can also set a default search path:

```sh
export MLANG_STDLIB_LIB_PATH=~/.local/lib/mlang
```

The stdlib module search path is controlled by `MLANG_STDLIB_PATH` and defaults
to `~/.local/share/mlang/stdlib` when installed.

Standalone libraries outside the `std` namespace use `MLANG_MODULE_PATH` and
default to `~/.local/share/mlang/modules`. The bundled `dsp` library is
installed there and imported with paths such as `dsp::filter` and `dsp::fft`.

## Build + Install
Build the compiler first from the repository root:

```sh
cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mlang mlang_std
```

That gives you a local compiler at `./build/mlang`.

Install the compiler and stdlib to a custom prefix such as `~/.local`:

```sh
cmake --install build --prefix "$HOME/.local"
```

If you specifically want the binary under `~/.local/bin`, ensure that
directory exists and either install with the prefix above or copy the binary
there explicitly:

```sh
mkdir -p "$HOME/.local/bin"
cp ./build/mlang "$HOME/.local/bin/mlang"
```

Add your custom bin directory to `PATH` in `~/.zshrc`:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Reload the shell config:

```sh
source ~/.zshrc
```

Confirm the installed compiler is the one being used:

```sh
which mlang
mlang --help
```

Once `mlang` is available on `PATH`, build and install the remaining
bootstrap-managed components through `mlang pkg`:

```sh
mlang pkg --config bootstrap/mlang.toml run build-all
mlang pkg --config bootstrap/mlang.toml run build-all --asan
mlang pkg --config bootstrap/mlang.toml run install-all
mlang pkg --config bootstrap/mlang.toml run build-and-install --option install_prefix=$HOME/.local --option bin_dir=$HOME/.local/bin
mlang pkg --config bootstrap/mlang.toml run install-tooling
```

For example, after installing `mlang` itself into `~/.local/bin`, you can use
that installed compiler to build and install the rest of the bootstrap-managed
tools into the same directory in one command:

```sh
mlang pkg --config bootstrap/mlang.toml run build-and-install --option install_prefix=$HOME/.local --option bin_dir=$HOME/.local/bin
ls "$HOME/.local/bin"
```

That install step places tools such as `mlangd-mla`, `mlang-format`,
`mlang-frontend-mla`, and `mlang-frontend` under `~/.local/bin`, and installs
matching manual pages under `~/.local/share/man/man1`.

You can also run individual steps instead of the whole chain:

```sh
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla --asan
mlang pkg --config bootstrap/mlang.toml run build-mlang-format
mlang pkg --config bootstrap/mlang.toml run build-mlang-format --asan
mlang pkg --config bootstrap/mlang.toml run build-mlang-frontend
mlang pkg --config bootstrap/mlang.toml run unit-tests
mlang pkg --config bootstrap/mlang.toml run robot-tests
```

The current bootstrap task set covers:
- `build-mlang`
- `build-mlangd-mla`
- `build-mlang-format`
- `build-mlang-frontend-mla`
- `build-mlang-frontend`
- `build-all`
- `build-and-install`
- `build-tooling`
- `unit-tests`
- `robot-tests`
- `docs`
- `install-mlang`
- `install-all`
- `install-mlangd-mla`
- `install-mlang-format`
- `install-mlang-frontend`
- `install-tooling`

## Documentation

The repository ships a Doxygen-based documentation build. Regenerate HTML
under `docs/out` with the built-in docs runner:

```sh
mlang run docs
```

`mlang run docs` searches from the current directory upward for `docs/Doxyfile`
or `Doxyfile`, then runs Doxygen from that project root. Use
`mlang run docs --doxyfile path/to/Doxyfile` to select a specific config.

The generated site mirrors the Markdown sources under `docs/`, stdlib sources
(`stdlib/std/`, `stdlib/src/`), and MLang tool sources
(`tools/mlang-frontend-mla/main.mla`, `tools/mlang-pkg-mla/main.mla`). Open
`docs/out/index.html` in a browser after running the command.

Compiler diagnostic reference lives in `docs/compiler_diagnostics.md` and is
published in the generated site as `docs/out/html/compiler_diagnostics.html`.

### GitHub Wiki Export

Generate GitHub Wiki-ready Markdown pages from the current repository
documentation with:

```sh
python3 scripts/generate_github_wiki.py
```

The default output directory is `docs/wiki`, so the generated wiki source lives
inside this repository and can be reviewed and committed with normal code
changes.

To update the repo-local wiki docs after editing `README.md`, `docs/`, stdlib
docs, tool docs, or man pages, rerun the same command:

```sh
python3 scripts/generate_github_wiki.py
git diff -- docs/wiki
```

The script refreshes the generated files in place. Commit the source
documentation changes and the refreshed `docs/wiki` files together.

During generation, bare fenced code blocks get inferred language tags where
possible, and inline MLang keywords, builtin types, and `std::...` references
are linked to the language guide or stdlib reference in the wiki output.
MLang examples are emitted with GitHub-supported `rust` fences so GitHub Wiki
applies syntax highlighting; GitHub does not recognize `mla` as a highlight
language.

To publish those files to GitHub Wiki later, clone the wiki repository and use
it as the output directory:

```sh
git clone https://github.com/mattilaa/mlang.wiki.git /tmp/mlang.wiki
python3 scripts/generate_github_wiki.py --output /tmp/mlang.wiki
cd /tmp/mlang.wiki
git add .
git commit -m "Update MLang wiki documentation"
git push
```

Use `--repo-url` if the GitHub repository owner or URL differs from
`https://github.com/mattilaa/mlang`.

## AddressSanitizer Verification
After a clean workspace, run the helper script that configures an AddressSanitizer build and runs the unit and robot test suites under ASan:

```sh
./scripts/run_asan.sh
```

The script delegates to `./scripts/build_install.sh --asan --unit-tests --robot-tests --no-install`, defaults to `build-asan` plus `artifacts-asan`, and propagates `ASAN_OPTIONS` to the compiler, unit tests, and robot runs. You can still override the default sanitizer tuning when invoking the script if you want stricter checks, for example `ASAN_OPTIONS=detect_container_overflow=1:strict_init_order=1 ./scripts/run_asan.sh`.

## Codesigning Built Binaries On macOS
Locally-linked Mach-O binaries on Apple Silicon are emitted with an adhoc
`linker-signed` signature. In some configurations the resulting CodeDirectory
hash does not match the final on-disk page contents (mismatch can come from
`strip`, `install_name_tool`, copying between filesystems, or certain
TLV/chained-fixup layouts), and macOS will SIGKILL the process the first time
it touches a "tainted" page:

```text
kernel: CODE SIGNING: cs_invalid_page(...): p=NNN[mlangd-mla] ... sending SIGKILL
kernel: CODE SIGNING: process NNN[mlangd-mla]: rejecting invalid page ... tainted:1 ...
```

The package manager exposes adhoc re-signing as a first-class build step so
the bootstrap pipeline can produce binaries that survive this check.

### `[[task]] sign` field
Any task in `mlang.toml` may declare one or more output paths to sign after
the task succeeds. The value can be a single string or a list, and standard
template substitutions (`{{build_dir}}`, `{{root}}`, `{{option.<key>}}`,
etc.) are expanded:

```toml
[[task]]
name = "build-mlangd-mla"
commands = [
  "{{build_dir}}/mlang tools/mlangd-mla/main.mla -L {{build_dir}} -lmlang_std -o {{build_dir}}/mlangd-mla"
]
sign = ["{{build_dir}}/mlangd-mla"]

[[task]]
name = "install-mlangd-mla"
shell = [
  "cp -f {{build_dir}}/mlangd-mla \"{{option.bin_dir}}/mlangd-mla\""
]
sign = ["{{option.bin_dir}}/mlangd-mla"]
```

Declaring `sign` does **not** force codesigning by itself — it only registers
which artifacts are signing candidates. The actual `codesign` invocation is
gated on a CLI flag, so reproducible builds without signing remain the default.

### `--sign` and `--force-sign` flags
`mlang pkg run` and `mlang pkg build` accept three signing modes:

| Flag | Behaviour |
|---|---|
| (none) | Default. `sign` declarations are ignored; no `codesign` call is made. |
| `--sign` | Runs `codesign --sign - <path>` for every `sign` entry of every executed task. Existing valid signatures are preserved. |
| `--force-sign` (alias `--forcesign`) | Runs `codesign --force --sign - <path>`. Any existing signature — including a tainted one — is replaced. Use this to recover a binary that is being SIGKILL'd. |
| `--no-sign` | Explicit opt-out, useful if a profile or wrapper script enabled signing globally. |

Examples — both forms the user may type are accepted:

```sh
# One-shot bootstrap that produces a known-good mlangd-mla and re-signs it:
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla --sign

# Recovery: replace a tainted/broken signature on the existing artifact:
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla --force-sign

# Same with the no-dash spelling (alias):
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla --forcesign

# Build the whole bootstrap chain and sign every signing-aware task in it:
mlang pkg --config bootstrap/mlang.toml run build-all --force-sign

# Install variant — signs the artifact in $HOME/.local/bin after the copy:
mlang pkg --config bootstrap/mlang.toml run install-mlangd-mla --force-sign
```

### Behaviour notes
- The hook is **macOS-only**. On Linux/Windows hosts the flags are accepted
  but no `codesign` invocation is made (the hook compiles to a no-op),
  so the same `mlang.toml` is portable across platforms.
- Missing `sign` paths print a warning but do not fail the task. This makes
  the field safe to keep on optional/host-gated build steps.
- A non-zero `codesign` exit fails the task. The post-task summary line will
  show the failing path so the source of the error is obvious.
- Manual recovery without the package manager is also possible:
  ```sh
  codesign --force --sign - ~/.local/bin/mlangd-mla
  ```
  The `--sign`/`--force-sign` flags simply automate this for every artifact
  declared in the bootstrap manifest.

The bootstrap profile `bootstrap/mlang.toml` already declares
`sign = [...]` on the `build-mlangd-mla`, `build-mlangd-mla-existing`,
`install-mlangd-mla`, and `install-mlangd-mla-existing` tasks, so a default
build/install with `--force-sign` will automatically keep the language-server
binary runnable on macOS.

## Formatter
`mlang-format` is now a separate binary compiled from Mlang source:
`tools/mlang-format-mla/main.mla` (port in progress from Python).
It supports clang-format style invocation and reads `.mlang-format`
from the file/workspace hierarchy.

```sh
# Print formatted output
mlang-format /path/to/file.mla

# In-place rewrite (clang-format style)
mlang-format -i /path/to/file.mla

# From stdin with style lookup anchored to a file path
cat /path/to/file.mla | mlang-format --assume-filename /path/to/file.mla
```

Current Mlang port scope:
- `--style file`, `-i/--in-place`, `--root`, `--assume-filename`
- `.mlang-format`: `IndentWidth`, `ContinuationIndentWidth`,
  `ConditionContinuationIndentWidth`, `IndentFunctionSignatureClosingParen`,
  `EnsureTrailingNewline`,
  `SpaceAfterComma`, `SpaceAfterColon`, `SpaceBeforeTernaryColon`,
  `SpaceAroundOperators`,
  `SpaceAroundRelationalOperators`, `SpaceInsideBracesSingleLine`,
  `CompactFatArrow` (default: `true`)

## Quickstart (Package Manager + curl example)
Build `mlang`, fetch a git dependency with the package manager, compile the
example, and run it (the program uses libcurl to fetch a URL).

```sh
# Build compiler first
cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mlang mlang_std

# Run package-manager demo
cd examples/package_manager_git_cjson
../../build/mlang pkg fetch
../../build/mlang pkg build
# Or: mlang pkg build -O3 --ninja
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

# Alternative flag form (same behavior)
mlang --tests
mlang --tests tests/std_math_tests.mla

# Run tests in a specific file or directory
mlang test tests/test_sample.mla
mlang test tests

# Alternative entry point
mlang run tests

# Benchmark #[test] functions in files prefixed with bench_*
mlang bench tests
mlang bench tests/bench_stdlib.mla --bench-iters 200000 --bench-warmup 20000
```

Benchmark anti-optimization helpers (Google Benchmark style) are available in
`std::bench`:
- `do_not_optimize_i64(v)`
- `do_not_optimize_i32(v)`
- `clobber_memory()`

Skip compiling tests in normal builds:

```sh
mlang --no-tests main.mla
```

GoogleTest-like non-fatal expectations are available via `std::testing`:
- `expect_true(cond)`
- `expect_false(cond)`
- `expect_eq(expected, actual)` (typed overloads)
- fatal verify variants are also available:
  `verify_true`, `verify_false`, `verify_eq` (and `VERIFY_*` aliases)
- lightweight mock API is also available:
  `Mock`, `mock_new`, `mock_expect_call`, `mock_called`, `mock_verify`

Interactive `std::io` input example (manual run, not part of Robot example
suite): `examples/std_io_input_demo.mla`.
The example uses `var user_input` directly (no `&mut` required) and trims with
`trim(user_input)`.
`std::io` now also supports stderr writes, stream buffering configuration
(`set_*_buffering`), and non-blocking stdin polling (`read_line_nonblocking`).
`std::time` now also provides local wall-clock formatting helpers:
`local_datetime()` (`MM/DD/YYYY:HH:MM:SS`), `local_datetime_ms()` (`...SS.MS`),
and `local_datetime_ns()` (`...SS.NS`).
Trait-like `std::io` handles (`Read`, `Write`, `Seek`, `BufRead`) example:
`examples/std_io_traits_demo.mla`.
Filesystem API (`std::fs::File`, `std::fs::BufReader`) example:
`examples/std_fs_demo.mla`.
TCP networking API (`std::net::TcpListener`, `std::net::TcpStream`,
non-blocking mode, read/write timeouts) example:
`examples/std_net_demo.mla`.
Random API (`std::rand` seed/range helpers) example:
`examples/std_rand_demo.mla`.
FFT API (`dsp::fft` forward/inverse on split real/imag arrays) example:
`examples/dsp_fft_demo.mla`.
Regex API (`std::regex::Regex`, compile/match/find/captures) example:
`examples/std_regex_demo.mla`.
Multithreaded TCP server/client examples:
`examples/std_net_mt_server.mla` and `examples/std_net_mt_client.mla`.
Advanced framed protocol stack examples (isolated in subdirectory):
`examples/protocol_mt/server.mla` and `examples/protocol_mt/client.mla`
with runner script `examples/protocol_mt/run_demo.sh`.
JSON API (`std::json::JsonDoc` parse/stringify/object-array navigation, iterators, and `from_file`) example:
`examples/std_json_demo.mla`.
Compiler-synthesized struct JSON serde (`#[derive(Json)]`, `to_json()`,
`Type::from_json(text)`, inherited fields, `@property` metadata) example:
`examples/std_json_derive_demo.mla`.
JSON-RPC/LSP transport runtime (`std::jsonrpc` Content-Length framing, timeout reads, cancellation registry, queue runtime) example:
`examples/std_jsonrpc_runtime_demo.mla`.
Manual stdio JSON-RPC worker runtime demo (`run_stdio_loop`, built-in `$/cancelRequest` routing):
`examples/std_jsonrpc_stdio_loop_demo.mla` (manual run, not part of Robot suite).
Incremental parse/query API for tooling (`std::compiler::Session`, open/change/close, diagnostics, hover, completion, document symbols, cross-document definition via `mod` files) example:
`examples/std_compiler_demo.mla`.
`?` is supported for `result` propagation (early-return on `Err`).

## Multithreaded TCP Demo (Local)
Build the compiler and run the new multithreaded TCP server/client examples:

```sh
cmake -S . -B build
cmake --build build -j

# Terminal 1: start server
mlang examples/std_net_mt_server.mla -o /tmp/std_net_mt_server_bin
/tmp/std_net_mt_server_bin --port 18788

# Terminal 2: run client
mlang examples/std_net_mt_client.mla -o /tmp/std_net_mt_client_bin
/tmp/std_net_mt_client_bin --port 18788
```

## Advanced Protocol Stack Demo (Local)
Build and run the isolated multithreaded protocol stack demo under
`examples/protocol_mt/`:

```sh
cmake -S . -B build
cmake --build build -j

# Build + run server/client together (script)
./examples/protocol_mt/run_demo.sh

# Optional tuning through env vars:
# PORT=19111 CLIENTS=2 ROUNDS=5 DELAY_MIN_MS=500 DELAY_MAX_MS=1000 ./examples/protocol_mt/run_demo.sh
```

Default script settings target a demo runtime around ~5 seconds
(`CLIENTS=1`, `ROUNDS=7`, `DELAY_MIN_MS=500`, `DELAY_MAX_MS=1000`).

Manual run (separate terminals):

```sh
# Terminal 1
mlang examples/protocol_mt/server.mla -o /tmp/protocol_mt_server
/tmp/protocol_mt_server --port 19095 --clients 4 --rounds 3

# Terminal 2
mlang examples/protocol_mt/client.mla -o /tmp/protocol_mt_client
/tmp/protocol_mt_client --port 19095 --clients 1 --rounds 7 --delay-min-ms 500 --delay-max-ms 1000
```

Before running Robot tests, set up and activate a Python virtual environment:

```sh
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
pip install -r tests/requirements.txt
```

Run the Robot Framework example suite (includes the multithreaded net case):

```sh
./tests/run_examples_robot.sh
```

Default output uses the standard Robot Framework console format, matching
`../mlangsh`, for example:

```text
==============================================================================
Suite
==============================================================================
Test Name                                                          | PASS |
------------------------------------------------------------------------------
```

If you want the older compact/timestamped progress output instead, opt in with:

```sh
ROBOT_CUSTOM_PROGRESS=1 ./tests/run_examples_robot.sh
```

That compact mode prints one line per test with `[n/total]` first. By default it does
not truncate test names. If you want long names shortened to keep each line within the
terminal width, enable it explicitly:

```sh
ROBOT_CUSTOM_PROGRESS=1 ROBOT_TRUNCATE_NAMES=1 ./tests/run_examples_robot.sh
```

`tests/run_examples_robot.sh` writes logs and machine-readable output into `results/`
(and any temporary binaries under `*bin`), but those directories/files are ignored by
`.gitignore`, so the working tree stays clean after the suite finishes.

### Running the new Multithreaded TCP Robot slice

To exercise the freshly documented frontend parity and multithreaded TCP coverage:

```sh
robot --test "MLang Frontend Trailing Tests *" tests/robot/examples.robot
robot --test "MLang Frontend CompileOnly TestsFlag *" tests/robot/examples.robot
```

## Examples
- Scope-exit destructor + owned resource cleanup:
  `examples/scope_exit_drop_demo.mla`
- Trait-like IO (`Read`/`Write`/`Seek`/`BufRead`) using in-memory cursor:
  `examples/std_io_traits_demo.mla`
- Lock-free queue cross-thread posting demo (SPSC message stream):
  `examples/lock_free_queue_post_demo.mla`
- JACK2 realtime process-thread demo with main-thread command posting:
  `examples/jack2_lockfree_thread_demo.mla` (+ bridge `examples/jack2_queue_bridge.c`)
- Filesystem read-lines flow (`File::open`, `BufReader::new`, `lines`):
  `examples/std_fs_demo.mla`
- TCP loopback client/server over libc sockets:
  `examples/std_net_demo.mla`
- Advanced framed protocol demo (multithreaded server, per-client workers, multi-client load):
  `examples/protocol_mt/server.mla`, `examples/protocol_mt/client.mla`
  (runner: `examples/protocol_mt/run_demo.sh`)
- Random number generation (`std::rand`) with deterministic seeding + ranges:
  `examples/std_rand_demo.mla`
- Regex compile + match + group extraction:
  `examples/std_regex_demo.mla`
- JSON parse/stringify, navigation, iterators, and `from_file` (`JsonDoc`, `JsonValue`):
  `examples/std_json_demo.mla`
- Struct JSON serde via `#[derive(Json)]` (`to_json()`, `from_json(...)`,
  inherited fields, `@property` metadata):
  `examples/std_json_derive_demo.mla`
- JSON-RPC/LSP stdio transport + cancellation/runtime queues (`std::jsonrpc`):
  `examples/std_jsonrpc_runtime_demo.mla`
- JSON-RPC stdio worker runtime loop (`std::jsonrpc::run_stdio_loop`):
  `examples/std_jsonrpc_stdio_loop_demo.mla`
- ANSI terminal escape helpers (`std::esc`):
  `examples/std_esc_demo.mla`
- ESC widget-style tracker UI composition demo (`std::esc` + `std::strbuf`):
  `examples/esc_widgets/tracker_ui_demo.mla`
- Terminal capabilities + termios helpers (`std::term`):
  `examples/std_term_demo.mla`
- Type aliases (`alias Distance = f32;` and equivalent `use type Distance = f32;`, generic aliases):
  `examples/type_alias_demo.mla`
- Compile-time `cexpr fn` evaluation:
  `examples/cexpr_twice_demo.mla`
- Floating-point `cexpr fn` evaluation:
  `examples/cexpr_float_demo.mla`
- Compile-time `cexpr` value declarations:
  `examples/cexpr_decl_demo.mla`
- Compile-time `cexpr if` branch selection:
  `examples/cexpr_if_demo.mla`
- Namespace blocks and aliases (`namespace geometry::units { ... }`, `alias gu = geometry::units;`) with qualified names:
  `examples/namespace_demo.mla`
- Lambda + fold expressions (`|x: T| { ... }`, `(... + xs)`, `(xs * ...)`):
  `examples/lambda_fold_demo.mla`
- Unit-testing mock expectations (`std::testing::Mock`):
  `examples/testing_mock_example.mla`
- Sundaram sieve with real hash-set membership (`HashSetI64`):
  `examples/sieve_sundaram.mla`

### JACK2 Lock-Free Queue Demo
This demo runs JACK2 audio processing in JACK's process callback thread while
the MLang main thread posts play commands through `std::sync::LockFreeQueue`.

Prereqs (macOS/Homebrew):

```sh
brew install jack pkg-config
jackd -d coreaudio
```

Build:

```sh
cc -O2 -I./include $(pkg-config --cflags jack) -c examples/jack2_queue_bridge.c -o /tmp/jack2_queue_bridge.o
ar rcs /tmp/libjack2_mlang_bridge.a /tmp/jack2_queue_bridge.o
mlang examples/jack2_lockfree_thread_demo.mla -L /tmp -ljack2_mlang_bridge $(pkg-config --libs jack) -o /tmp/jack2_lockfree_demo
```

Run:

```sh
/tmp/jack2_lockfree_demo
```

The bridge attempts auto-connect to playback ports at startup. If you still get
silence, wire ports manually:

```sh
jack_lsp -c
jack_connect mlang_jack2_queue_demo:out_l system:playback_1
jack_connect mlang_jack2_queue_demo:out_r system:playback_2
```

### JACK2 Drum Machine Sampler Demo
Loads WAV samples from `examples/sampler_example` via C/JACK backend, while
the MLang sequencer parses `sequence.txt` and advances on JACK-clocked lock-free `tick` events.

Prereqs (macOS/Homebrew):

```sh
brew install jack pkg-config
jackd -d coreaudio
```

Build:

```sh
cc -O2 -I./include $(pkg-config --cflags jack) -c examples/sampler_example/jack2_drum_machine_bridge.c -o /tmp/jack2_drum_machine_bridge.o
ar rcs /tmp/libjack2_drum_machine.a /tmp/jack2_drum_machine_bridge.o
mlang examples/sampler_example/jack2_drum_machine.mla -L /tmp -ljack2_drum_machine $(pkg-config --libs jack) -o /tmp/jack2_drum_machine_demo
```

Run:

```sh
/tmp/jack2_drum_machine_demo
```

At startup, the sequencer prints controls: `SPACE` toggles start/stop and `q` exits.
The main loop uses `std::event_loop::EventLoop` (async timer thread) and JACK lock-free tick events.

If auto-connect fails, connect manually:

```sh
jack_lsp -c
jack_connect mlang_drum_machine:out_l system:playback_1
jack_connect mlang_drum_machine:out_r system:playback_2
```

### JACK2 Stereo FFT Analyzer Demo
Plays a stereo WAV via JACK2 and renders a real-time 100x25 ASCII spectrum
analyzer with colorized stereo differences (left/right/overlap) using `dsp::fft`.
`run_demo.sh` also accepts non-WAV input (for example `.m4a`) via `ffmpeg` decode.

Demo directory:
- `examples/fft_example/main.mla`
- `examples/fft_example/fftviz_bridge.c`
- `examples/fft_example/illusion.wav`
- `examples/fft_example/run_demo.sh`

Build + run:

```sh
./examples/fft_example/run_demo.sh
```

CLI options:
- `--flat` / `--music` render mode
- `--time=hh::mm::ss` (also accepts `mm::ss`) start playback at given position
- `--log=x` FFT bar-height scale (`100` default, `>100` compress, `<100` expand)
- `--buffer=n` request JACK buffer size in frames (for example `32`, `64`, `128`)
- `-h` / `--help` show demo help

If JACK playback ports differ on your system, use explicit output ports:

```sh
FFTVIZ_OUT_L="system:playback_3" FFTVIZ_OUT_R="system:playback_4" ./examples/fft_example/run_demo.sh --flat
```

Custom WAV:

```sh
./examples/fft_example/run_demo.sh /path/to/stereo.wav
```

Custom non-WAV (example: m4a, requires `ffmpeg` in PATH):

```sh
./examples/fft_example/run_demo.sh /path/to/file.m4a
```

Start at a specific time:

```sh
./examples/fft_example/run_demo.sh /path/to/stereo.wav --music --time=00::01::30
```

Adjust FFT bar-height scale:

```sh
./examples/fft_example/run_demo.sh --log=120   # more compressed bars
./examples/fft_example/run_demo.sh --log=80    # taller bars
```

Request low-latency JACK buffer:

```sh
./examples/fft_example/run_demo.sh --buffer=32
```

## Object-Oriented Language Features
This section documents the object-oriented capabilities added on the
`feature/add_oop_features` branch (commits `bd4b339`..`7a423d9`). Each entry
follows a Doxygen-style structure: `@brief`, `@details`, `@code` (example),
`@note`, and `@see` (cross references).

### Feature Matrix

| Feature | Commit | Demo |
|---|---|---|
| Method visibility on `impl` blocks | `bd4b339` | `examples/method_visibility_demo/` |
| Associated (static) function calls | `3519d6b` | `examples/associated_functions_demo/` |
| Trait `impl` method signature validation | `643a182` | `tests/std_compiler_tests.mla` |
| Generic trait bounds (`T: Trait`) | `24732cf` | `examples/generic_trait_bounds_demo/` |
| Qualified generic static method calls | `7a423d9` | `examples/module_path_generic_static_demo/` |
| Trait objects (`dyn Trait`) | `d80f52c`+ | `examples/dyn_trait_demo/`, `examples/dyn_trait_field_demo/` |
| Default methods on traits | (this branch) | `examples/trait_advanced_demo/` |
| Super-traits (`trait Foo: Bar`) | (this branch) | `examples/trait_advanced_demo/` |
| Multiple trait bounds (`T: A + B`) | (this branch) | `examples/trait_advanced_demo/` |

---

### Method Visibility On `impl` Blocks

> **@brief** Allow `pub` and private methods inside `impl` blocks, with
> visibility checked using the **defining module** of the method (not the call
> site).

> **@details** Methods declared without `pub` are callable only from within the
> module where the `impl` block lives; methods marked `pub fn` are callable
> from any module that can name the receiver type. The IR layer was fixed so
> that the visibility check uses the method's own module context, allowing a
> private helper to be invoked by another method on the same type even when
> that public entry point is itself called from a different module.

> **@code**
```mla
// lib/counter.mla
pub struct Counter {
    var value: i32;
};

impl Counter {
    // Private helper: callable only inside this module.
    fn inc_private(self: Counter) -> Counter {
        return Counter { value: self.value + 1 };
    }

    // Public wrapper: callable from other modules.
    pub fn inc(self: Counter) -> Counter {
        return self.inc_private();
    }

    pub fn read(self: Counter) -> i32 {
        return self.value;
    }
}
```

```mla
// main.mla
mod lib::counter;
use lib::counter::Counter;

fn main() -> i32 {
    let c: Counter = Counter { value: 1 };
    let d: Counter = c.inc();          // OK: pub
    // let e: Counter = d.inc_private(); // ERROR: private to lib::counter
    return d.read() == 2 ? 0 : 1;
}
```

> **@note** Public methods may freely delegate to private methods on the same
> type. Visibility resolution does not flow through the caller's module, only
> through the defining module of each candidate method.

> **@see** `examples/method_visibility_demo/` and `src/ir.cpp` (visibility
> module-context fix).

---

### Associated (Static) Function Calls

> **@brief** Call a function defined inside `impl Type { ... }` directly via
> the type name (e.g. `Counter::new(0)`), without an instance.

> **@details** Associated functions are functions that do not take a `self`
> receiver. The compiler resolves `Type::func(args)` against the `impl Type`
> namespace and emits a normal direct call. Associated functions can be used
> as constructors (`new`, `zero`), factory helpers (`Counter::build_value`),
> or any utility that is logically owned by the type but does not depend on a
> specific instance.

> **@code**
```mla
pub struct Counter {
    var value: i32;
};

impl Counter {
    fn build_value(value: i32) -> Counter {
        return Counter { value: value };
    }

    pub fn new(value: i32) -> Counter {
        return Counter::build_value(value);    // associated call
    }

    pub fn zero() -> Counter {
        return Counter::new(0);                // associated call
    }

    pub fn inc(self: Counter) -> Counter {
        return Counter::new(self.value + 1);   // associated call from method
    }

    pub fn read(self: Counter) -> i32 {
        return self.value;
    }
}

fn main() -> i32 {
    let start: Counter = Counter::new(10);
    let zero:  Counter = Counter::zero();
    let next:  Counter = start.inc();
    return next.read() == 11 ? 0 : 1;
}
```

> **@note** Associated functions follow the same `pub` visibility rules as
> instance methods (see "Method Visibility On `impl` Blocks").

> **@see** `examples/associated_functions_demo/`,
> `tests/std_compiler_tests.mla` (associated-call coverage).

---

### Trait `impl` Method Signature Validation

> **@brief** Verify at compile time that every `impl Trait for Type { ... }`
> block fully and correctly implements the methods declared by the trait.

> **@details** When the compiler lowers an `impl Trait for Type` block it now
> looks up each declared trait method and checks:
> 1. **Presence** — every required method must be implemented.
> 2. **Receiver shape** — an instance method (one with a `self` receiver) in
>    the trait must be implemented as an instance method on the type, and
>    vice versa.
> 3. **Return type** — the implementation's return type must match the
>    trait's declared return type.
>
> A descriptive diagnostic is emitted on mismatch so the user can locate
> the offending `impl` block quickly.

> **@code** Diagnostics produced for malformed implementations:
```text
trait 'Summary' for struct 'Post' requires method 'summarize'
method 'Post::summarize' does not match trait 'Summary': return type 'i32' does not match expected 'str8'
method 'Post::summarize' does not match trait 'Summary': expected instance method
```

```mla
trait Summary {
    fn summarize(self: Self) -> str8;
}

pub struct Post { var id: i32; };

// OK — matches signature exactly.
impl Summary for Post {
    fn summarize(self: Post) -> str8 {
        return "ok";
    }
}
```

> **@note** Validation runs even when the trait method is never called, so
> structurally incorrect `impl` blocks cannot ride along quietly until first
> use.

> **@see** Trait-impl test cases in `tests/std_compiler_tests.mla`
> (`test_compiler_trait_impl_*`).

---

### Generic Trait Bounds (`T: Trait`)

> **@brief** Constrain generic type parameters with one or more trait bounds
> on `struct`, `impl`, and (transitively) function definitions, so generic
> code may invoke trait methods on its parameters.

> **@details** A type parameter declared as `<T: Summary>` only accepts
> concrete types whose `impl Summary` block exists at instantiation time.
> Inside the generic `impl<T: Summary>` body, calls of the form
> `value.summarize()` are dispatched through the trait. Attempting to
> instantiate the generic with a type that does not satisfy the bound is a
> compile error.

> **@code**
```mla
trait Summary {
    fn summarize(self: Self) -> str8;
}

pub struct Post {
    var id: i32;
    var title: str8;
};

impl Summary for Post {
    fn summarize(self: Post) -> str8 {
        return self.title;
    }
}

pub struct Holder<T: Summary> {
    var value: T;
};

impl<T: Summary> Holder {
    pub fn new(value: T) -> Holder<T> {
        return Holder<T> { value: value };
    }

    // The bound `T: Summary` makes this trait call legal.
    pub fn summary(self: Holder<T>) -> str8 {
        return self.value.summarize();
    }
}

fn main() -> i32 {
    let post: Post = Post { id: 7, title: "trait-bound holder" };
    let holder: Holder<Post> = Holder<Post>::new(post);

    // let bad: Holder<i32> = Holder<i32>::new(1); // rejected: i32: Summary missing
    return holder.summary() == "trait-bound holder" ? 0 : 1;
}
```

> **@note** Bounds are written on **type parameters**, not on trait or
> function names. The compiler accepts the same `<T: TraitName>` syntax in
> the struct header and in the `impl<...>` header.

> **@see** `examples/generic_trait_bounds_demo/`,
> `tests/std_compiler_tests.mla` (generic trait-bound diagnostics).

---

### Trait Objects (`dyn Trait`)

> **@brief** Pass values behind an explicit trait-object type so a function
> can accept any concrete implementation of that trait.

> **@details** A parameter or return value typed `dyn Summary` stores a data
> pointer plus the trait vtable. Inside the callee, trait-method calls like
> `item.score()` dispatch through that vtable. Concrete values can be returned
> through a dyn return type, and existing dyn values can be passed through
> wrapper functions. This is intentionally narrower than a full object system:
> it currently covers function parameters, local dyn variables, dyn return
> values, direct method calls on dyn values, and owned trait-object fields in
> structs.

> **@code**
```mla
trait Summary {
    fn score(self: Self) -> i32;
}

pub struct Post {
    var score_value: i32;
};

impl Summary for Post {
    fn score(self: Post) -> i32 {
        return self.score_value;
    }
}

pub fn make_summary(post: Post) -> dyn Summary {
    return post;
}

pub fn pass_summary(item: dyn Summary) -> dyn Summary {
    return item;
}

pub fn show_score(item: dyn Summary) -> i32 {
    return item.score();
}

pub struct Holder {
    var item: dyn Summary;
};

pub fn make_holder(post: Post) -> Holder {
    return Holder { item: post };
}

fn main() -> i32 {
    let post: Post = Post { score_value: 7 };
    let item: dyn Summary = make_summary(post);
    let item2: dyn Summary = pass_summary(item);
    show_score(item2);
    let held: Holder = make_holder(Post { score_value: 13 });
    held.item.score();
    return 0;
}
```

> **@note** Use `dyn Trait` when you want runtime dispatch at the function
> boundary. Use `T: Trait` when the type should stay generic and monomorphized.
> When returning a concrete value as `dyn Trait`, the compiler stores a durable
> copy behind the trait object so the returned object does not point at callee
> stack storage. Across modules, callers may annotate locals with a qualified
> trait path such as `dyn lib::summary::Summary`. Concrete values stored into a
> `dyn Trait` field are copied into durable storage before the trait object is
> stored in the struct.

> **@see** `examples/dyn_trait_demo/`,
> `examples/dyn_trait_field_demo/`,
> `tests/std_compiler_tests.mla` (trait object dispatch and dyn return tests).

---

### Qualified Generic Static Method Calls

> **@brief** Allow fully qualified, module-prefixed associated calls on
> generic types, including the type-argument list — for example
> `lib::summary::Holder<Post>::new(post)`.

> **@details** Resolution walks the module path (`lib::summary`), looks up
> the generic receiver type (`Holder<Post>`), and dispatches the associated
> function (`new`) on the resolved `impl<T: Summary> Holder` block. Both
> short-form (`Holder<Post>::new(...)`) and module-qualified
> (`mod::path::Holder<Post>::new(...)`) call shapes are supported, and they
> work whether the `use` import is present or not.

> **@code**
```mla
mod lib::summary;
use lib::summary::Post;
use lib::summary::Holder;

fn main() -> i32 {
    let post: Post = Post { title: "module-path generic static call" };

    // Fully qualified: module path + generic type argument + associated call.
    let holder: Holder<Post> = lib::summary::Holder<Post>::new(post);

    let text: str8 = holder.summary();
    println!("summary={}", text);
    return text == "module-path generic static call" ? 0 : 1;
}
```

> **@note** This composes naturally with generic trait bounds: the
> instantiation `Holder<Post>` is checked against the `T: Summary` bound at
> the qualified call site, just as it is for the unqualified form.

> **@see** `examples/module_path_generic_static_demo/`,
> `examples/generic_trait_bounds_demo/`,
> `tests/std_compiler_tests.mla` (qualified-static-call coverage).

---

### Default Methods On Traits

> **@brief** Allow trait declarations to provide a method body. Implementing
> types may either rely on the trait-supplied default or override it.

> **@details** Trait method declarations now accept either a signature-only
> form (`fn foo(self: Self) -> T;`) or a body form
> (`fn foo(self: Self) -> T { ... }`). When an `impl Trait for X` block does
> **not** define a method that the trait declares, the compiler will:
> 1. If the trait method has a body — synthesize a method on the impl that
>    delegates to the default body, rebinding `self: Self` to `self: X`.
> 2. If the trait method has no body — emit the existing
>    `'X' requires method 'foo'` diagnostic.
>
> Default bodies may freely call other trait methods through `self.foo()`,
> so it is idiomatic to expose one or two required primitives and provide
> high-level operations as defaults.

> **@code**
```mla
trait Greeter {
    fn name(self: Self) -> str8;

    // Default body — implementers can rely on this without overriding.
    fn greet(self: Self) -> str8 {
        return self.name();
    }
}

pub struct Friend { var who: str8; };
impl Greeter for Friend {
    fn name(self: Friend) -> str8 { return self.who; }
    // `greet` is inherited from the trait default.
}

pub struct Robot { var serial: str8; };
impl Greeter for Robot {
    fn name(self: Robot) -> str8 { return self.serial; }
    // Override the default.
    fn greet(self: Robot) -> str8 { return "BEEP"; }
}
```

> **@note** The default body itself is parsed once on the trait. The compiler
> shares the body AST between every impl that doesn't override; only the
> `self` parameter is cloned and rebound per impl, so the cost of a default
> method is one synthesized `StructMethodNode` per impl, not a body deep-copy.

> **@see** `examples/trait_advanced_demo/lib/identity.mla` (the `Tagged`
> trait demonstrates a default method delegating to other trait methods);
> `tests/std_compiler_tests.mla::test_compiler_trait_default_method_*`.

---

### Super-Traits (`trait Foo: Bar`)

> **@brief** Declare that any implementer of one trait must also implement
> another. Both single (`Foo: Bar`) and multiple (`Foo: Bar + Baz`)
> super-trait lists are accepted.

> **@details** Super-trait constraints are recorded on the `TraitDefNode`
> at parse time. When the compiler validates `impl Foo for X`, it checks
> that `X` also has an explicit `impl S for X` for every super-trait `S`
> of `Foo`. Generic impls (`impl<T> Foo for X`) are skipped at this stage
> because their concrete type isn't fixed; the equivalent check fires at
> instantiation time through the existing trait-bound machinery.
>
> A missing super-impl produces a precise diagnostic:
> ```text
> trait 'Loud' for struct 'Word' requires struct to also implement super-trait 'Display'
> ```

> **@code**
```mla
trait Display {
    fn show(self: Self) -> str8;
}

// Super-trait: every Loud is also a Display.
trait Loud: Display {
    fn shout(self: Self) -> str8;
}

pub struct Word { var t: str8; };

impl Display for Word {
    fn show(self: Word) -> str8 { return self.t; }
}

// OK — Display impl is present.
impl Loud for Word {
    fn shout(self: Word) -> str8 { return self.t; }
}
```

> **@note** Super-traits compose: `trait C: B` and `trait B: A` together
> require an implementer of `C` to provide `impl A`, `impl B`, and
> `impl C` separately. The compiler does not auto-derive any of them.

> **@see** `examples/trait_advanced_demo/lib/identity.mla` (the
> `Tagged: Display` chain); `tests/std_compiler_tests.mla::test_compiler_super_trait_*`.

---

### Multiple Trait Bounds (`T: A + B`)

> **@brief** Type parameters may be constrained by more than one trait at
> once, using `+` to chain bounds in struct, `impl`, and type-alias headers.

> **@details** The previous syntax `<T: Foo>` accepted only a single trait.
> The grammar now accepts a `+`-joined chain (`<T: Foo + Bar + Baz>`), and
> the bound checker requires the concrete type substituted for `T` to
> implement **every** trait in the chain.
>
> Each missing bound is reported individually, so a single instantiation
> error can surface multiple suggestions:
> ```text
> type argument 'Post' for struct 'Holder' must implement trait 'Tagged' required by type parameter 'T'
> ```

> **@code**
```mla
trait A { fn a(self: Self) -> i32; }
trait B { fn b(self: Self) -> i32; }

pub struct X { var v: i32; };
impl A for X { fn a(self: X) -> i32 { return self.v; } }
impl B for X { fn b(self: X) -> i32 { return self.v; } }

// Bound chain accepted on struct, impl, and `alias` / `use type` aliases.
pub struct Box<T: A + B> { var inner: T; };
impl<T: A + B> Box {
    pub fn new(inner: T) -> Box<T> {
        return Box<T> { inner: inner };
    }
}

fn main() -> i32 {
    let b: Box<X> = Box<X>::new(X { v: 1 });
    return 0;
}
```

> **@note** Bounds are stored internally as a `+`-joined string (e.g.
> `"A+B"`) so the existing `map<str8, str8>` AST field accommodates
> multiple bounds without a schema change. The split-and-check is done on
> the consumer side in `validateTypeArgumentTraitBounds`. Whitespace
> around `+` is allowed: `<T: A + B>` and `<T:A+B>` parse identically.

> **@see** `examples/trait_advanced_demo/lib/identity.mla` (the
> `Holder<T: Display + Tagged>` container);
> `tests/std_compiler_tests.mla::test_compiler_multiple_trait_bounds_*`.

---

## Rust-like Attributes
Mlang currently supports these Rust-like attributes:

| Attribute | Target | Purpose |
|---|---|---|
| `#[derive(Debug)]` | `struct` definitions | Enables debug formatting (`{:?}`/`{:#?}` and `println!(value)` for structs). |
| `#[derive(Json)]` | `struct` definitions | Synthesizes `to_json()` / `from_json(...)` for supported structs, including inherited fields and `@property` metadata. |
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
    let expected: str8 = "The origin is: Point { x: 0, y: 0 }";

    assert_eq!(format!("The origin is: {origin}"), expected);
    println!(origin);
    debug!("origin = {origin}");
    return 0;
}
```

### `#[derive(Json)]`

```mla
#[derive(Json)]
struct Base {
    @property(hidden) var secret: i32;
    var x: i32;
};

#[derive(Json)]
struct Leaf : Base {
    var name: str8;
};

fn main() -> i32 {
    var leaf: Leaf {};
    leaf.setSecret(7);
    leaf.x = 3;
    leaf.name = String::from("ok");

    let text: str8 = leaf.to_json();
    let parsed: result<Leaf, str8> = Leaf::from_json(text);
    if parsed.is_err() {
        return 1;
    }
    println!("{}", text);
    return 0;
}
```

`#[derive(Json)]` currently supports JSON round-tripping for structs whose
fields are `bool`, integer primitives, `f32`/`f64`, `str8`, or nested structs
that also derive `Json`. For derived structs, base fields are serialized
directly into the object. Fields declared with `@property(...)` are also
described in a sibling `@property` metadata tree in the emitted JSON.

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

[tool.mlang]
module_paths = ["."]
min_mlang_version = "0.2.0"
opt_level = "O2"
target_arch = "x64"
use_ninja = true
lib_paths = ["vendor/lib"]
libs = ["foo"]
linker_flags = ["-Wl,-rpath,vendor/lib"]
compiler_flags = ["-Wno-unwrap"]
static_deps = true
```

Source dependencies also accept `spinner = false` to disable the rolling
status cursor for that dependency's fetch/build steps. This is useful when the
underlying tool already has its own progress output, such as `curl`. Built-in
dependency commands with `spinner = false` also stay out of the stdout/stderr
log files so transfer progress does not pollute package logs. Other package
operations keep the spinner by default unless CLI log routing is enabled.

`[tool.mlang]` can be used to set package-build defaults for `mlang pkg build`.
Supported keys are:

- `module_paths`: existing module search path support.
- `min_mlang_version`: minimum `mlang` version required to build the package.
- `opt_level`: `O0`, `Og`, `O1`, `O2`, `O3`, `Os`, or `Oz` (with or without
  the leading `-`).
- `target_arch`: `x86`, `x86-64`, `x64`, `x86_64`, `amd64`, `aarch64`, or `arm64`.
- `build_dir`: directory where `pkg build` writes binaries and `pkg run`
  stores generated task scripts. Defaults to `build`.
- `deps_dir`: directory where `pkg fetch` stores sources and `pkg build`
  reuses dependency artifacts. Defaults to `<build_dir>/deps`.
- `log_dir`: base directory for package-manager log files.
- `stdout_log`: file that receives package-manager info lines and command
  stdout.
- `stderr_log`: file that receives package-manager error lines and command
  stderr.
- `warn_log`: file that receives package-manager warning lines.
- `path_entries`: directories prepended to `PATH` for pkg fetch/build/run.
  `bin_paths` is accepted as an alias.
- `make_program`: make executable used by `build = "make"` dependencies and by
  `[[task]]` commands through `{{make}}`.
- `use_ninja`: use the Ninja generator for dependency builds. `ninja = true`
  is accepted as an alias.
- `lib_paths`: extra library search paths, emitted as `-L...`.
- `libs`: extra libraries, emitted as `-l...`.
- `linker_flags`: raw linker-related flags forwarded to the compiler invocation.
- `compiler_flags`: additional raw compiler flags forwarded during `pkg build`.
- `static_deps`: link fetched package dependencies via discovered `.a` archives.
- `static_cpp_runtime`: add `-static-libstdc++ -static-libgcc` during package
  linking. This is mainly useful on GNU/Linux toolchains.
- `task_print_to_stdout_log`: mirror task `print` / `message` lines into the
  stdout log while keeping them visible on the console.

If you do not set `build_dir`, `deps_dir`, or any CLI overrides, package
behavior stays unchanged: `pkg build` writes outputs to `build/`, and
`pkg fetch` / `pkg build` use `build/deps/` for fetched dependencies.

If `[tool.mlang].min_mlang_version` is set and the running compiler is older
than that version, `mlang pkg build` fails before starting the build.

If `use_ninja = true` is set, `mlang pkg build` verifies that `ninja` or
`ninja-build` exists in `PATH` before dependency builds begin.

If `path_entries` is set, those directories are prepended to `PATH` for
dependency fetch/build commands, `pkg-config`, Ninja detection, final package
linking, and `pkg run` tasks. This is useful on macOS when Homebrew tools
should be preferred over `/usr/bin`.

`build_dir` and `deps_dir` are resolved relative to the package root unless
they are already absolute. This lets one project share a single dependency
cache such as `.pkg/deps` while building into separate directories like
`build-debug` and `build-release`.

If `log_dir` is set, relative `stdout_log`, `stderr_log`, and `warn_log` paths
are resolved under that directory. Those log destinations are only activated
when you pass a pkg log flag such as `--log-dir`, `--stdout-log`,
`--stderr-log`, `--warn-log`, or `--task-print-to-stdout-log`. Without those
CLI flags, package-manager and task output stays on the console as before.
When logging is enabled and `log_dir` is set, any missing log filename falls
back to `pkg.stdout.log`, `pkg.stderr.log`, or `pkg.warn.log` inside that
directory.
Rolling spinner/status lines are console-only, are not written to the log
files, and the rolling spinner falls back to plain status lines while CLI log
routing is enabled.

If `make_program` is set, `mlang pkg` uses that executable for built-in
`build = "make"` dependency builds, and `[[task]]` commands can reference it as
`{{make}}`.

Libraries declared in `[tool.mlang].libs` are also validated before the real
package link step. If a declared `-l...` entry cannot be linked with the
configured `lib_paths`, `mlang pkg build` fails early with the missing library
name.

Packages can also declare multiple executable targets:

```toml
[package]
name = "multi_bin_demo"
version = "0.1.0"

[tool.mlang]
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

When `[[bin]]` entries are present, `mlang pkg build` builds each executable
into `<build_dir>/<bin-name>`.

Target-scoped config keys supported inside `[[bin]]` are:

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

Target-scoped values are merged with `[tool.mlang]` defaults:

- scalar values such as `opt_level`, `target_arch`, `min_mlang_version`,
  `use_ninja`, and target-specific path settings override the package default
  for that target
- list values such as `compiler_flags`, `linker_flags`, `lib_paths`, and `libs`
  are appended after the package defaults
- boolean values such as `static_deps` and `static_cpp_runtime` override the
  package default when explicitly set on the target

For `pkg build`, an explicit CLI optimization flag such as `-Og`, `-O3`,
`-Os`, or `-Oz` overrides `[tool.mlang].opt_level`.

Workspace roots can also declare recursive package discovery:

```toml
[workspace]
members = ["packages"]
```

Each listed member is treated as a directory root under the current project.
`mlang pkg fetch`, `mlang pkg build`, and `mlang pkg clean` recursively scan
for `mlang.toml` files under those roots and run package operations for each
discovered subpackage.

Directory tree example:

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
mlang pkg fetch
mlang pkg build
mlang pkg clean
```

Source dependencies can now come from Git or from a `tar.gz` URL:

```toml
[dependencies]
cjson_git = { git = "https://github.com/DaveGamble/cJSON.git", tag = "v1.7.18", build = "cmake" }
cjson_tar = { url = "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz", archive = "tar.gz", strip_components = "1", build = "cmake" }
```

Supported source dependency keys are:

- `git`
- `url`
- `archive`
- `rev`
- `tag`
- `build`
- `cmake_args`
- `subdir`
- `strip_components`

If `build = "none"` is set, the dependency is fetched but skipped by the
built-in dependency builders during `mlang pkg build`.

`pkg add --add-lib` can scaffold the workspace subproject automatically. It:

- creates `packages/<name>/mlang.toml`
- creates `packages/<name>/src/main.mla`
- adds the fetched dependency to that generated subproject manifest
- ensures the root manifest contains `[workspace] members = ["packages"]`

Override the generated location with:

```sh
mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --add-lib --project-dir packages/json/cjson_demo
```

## Package Workspaces And Fetched Subprojects

This is the recommended layout when one repository contains several package
subdirectories and each subpackage may fetch its own source dependencies.

Combined tree:

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
mlang pkg fetch
mlang pkg build
```

See:
- `examples/package_manager_workspace_fetch`
- `examples/package_manager_workspace_fetch/README.md`

That example shows one root `mlang.toml` discovering subpackages recursively,
while one subpackage fetches from GitHub with `git = "..."` and another uses a
released `tar.gz` source archive.

### Generate A Subproject With `pkg add --add-lib`

You can generate the directory tree instead of writing it by hand.

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
mlang pkg add cjson --git https://github.com/DaveGamble/cJSON.git --add-lib
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
mlang pkg fetch
mlang pkg build
```

Packages can also declare shell-driven custom tasks:

```toml
[[task]]
name = "kernel-build"
workdir = "{{root}}"
commands = [
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 defconfig",
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 -j${JOBS:-4} Image"
]

[tool.mlang]
make_program = "gmake"
```

Run a task with:

```sh
mlang pkg run kernel-build
```

Linux AArch64 kernel example sequence:

```sh
cd examples/package_manager_linux_aarch64_qemu
mlang pkg fetch
mlang pkg run boot-flow
```

Linux AArch64 kernel example installation on macOS:

```sh
brew install llvm lld libelf gnu-sed make qemu cpio
```

Linux AArch64 kernel example installation on Debian/Ubuntu:

```sh
sudo apt-get install clang lld make qemu-system-arm cpio gzip gcc-aarch64-linux-gnu
```

Supported `[[task]]` keys:

- `name`
- `print`
- `message`
- `phase`
- `workdir`
- `language`
- `source`
- `output`
- `inputs`
- `compile_only`
- `parallel`
- `depends_on`
- `phase_depends_on`
- `next`
- `next_phases`
- `join_on`
- `phase_join_on`
- `env`
- `shell` / `script`
- `opt_level`
- `target_arch`
- `path_entries`
- `compiler_flags`
- `linker_flags`
- `lib_paths`
- `libs`
- `static_deps`
- `static_cpp_runtime`
- `command`
- `commands`

Host-conditional task overrides:

- `[task.host.darwin]`
- `[task.host.linux]`
- `[task.host.windows]`

Override behavior:

- override `workdir` replaces the base `workdir`
- override `print` replaces the base `print`
- override `message` replaces the base `message`
- override `parallel` replaces the base `parallel`
- override `depends_on` appends after the base `depends_on`
- override `join_on` appends after the base `join_on`
- override `next` appends after the base `next`
- override `env` appends after the base `env`
- override `shell` replaces the base inline shell script
- override `command` / `commands` replace the base task commands

Declarative task builds are also supported. Set `language = "mlang"`,
`"c"`, or `"c++"` together with `source`, `output`, `inputs`, and optionally
`compile_only = true`, and `mlang pkg` generates the compiler or linker
invocation for that task. Task-local `libs`, `lib_paths`, `compiler_flags`,
`linker_flags`, `static_deps`, and `static_cpp_runtime` are applied to that
generated build step, and any extra `commands` still run afterward.

List-valued task keys such as `inputs`, `libs`, `compiler_flags`,
`linker_flags`, `commands`, `shell`, and `path_entries` accept multiline
comma-separated TOML arrays, and both `"double-quoted"` and `'single-quoted'`
string items are supported.

TOML `#` comments are also supported on their own line and at the end of an
assignment line, as long as the `#` appears outside quoted string content.

Task placeholders:

- `{{root}}`
- `{{manifest}}`
- `{{build_dir}}`
- `{{deps_dir}}`
- `{{make}}`

`{{build_dir}}` and `{{deps_dir}}` reflect the configured `[tool.mlang]`
paths for that package.

`print` writes a line directly to the console before a task runs. `message`
is supported as an alias. This is useful for long task graphs and for making
progress visible without embedding `echo` in shell.

`mlang pkg run <task>` also honors task dependencies. If a task declares
`depends_on`, `phase_depends_on`, `join_on`, or `phase_join_on`, those tasks
run before the requested task body starts. `mlang pkg run` does not
implicitly run `mlang pkg fetch`, so a clean workspace should fetch first if
tasks expect sources under `{{deps_dir}}`.

CLI overrides are available on `pkg fetch`, `pkg build`, `pkg run`, and
`pkg clean`: `--log-dir DIR`, `--stdout-log FILE`, `--stderr-log FILE`,
`--warn-log FILE`, and `--task-print-to-stdout-log`.

`mlang pkg` also accepts `--config FILE` before the subcommand. Use it when a
single project root keeps multiple manifests, for example:

```sh
mlang pkg --config build-arm64.toml build
mlang pkg --config build-x64.toml build
```

If `--config` is omitted, the package manager still uses `mlang.toml`.

The Linux kernel example uses:

- `toolchain-check` to verify the required LLVM, GNU `sed`, and `libelf`
  pieces exist before starting the kernel build
- `darwin-native-prepare` to generate compatibility headers and patch
  `scripts/mod/file2alias.c` for native Apple Silicon builds
- `kernel-build` to run `defconfig` and build `arch/arm64/boot/Image`
- `initramfs` to pack the example initramfs
- `qemu-run` to boot the resulting kernel under QEMU after its prerequisites
  complete, optionally in parallel

Linux is the recommended host for that example. macOS support is best-effort
but now includes a native Apple Silicon path based on
<https://seiya.me/blog/building-linux-on-macos-natively>. That path uses
Homebrew `llvm`, `lld`, `libelf`, `gnu-sed`, and generated compatibility
headers plus a small source patch in `scripts/mod/file2alias.c`, and it adds
`-Wno-error=incompatible-pointer-types` for Darwin host tools to get past
known Homebrew `libelf` typedef mismatches in `scripts/sorttable`. Linux is
still the supported host for reproducible full-kernel builds.

Example workflow with config-driven defaults:

```sh
mlang pkg fetch
mlang pkg build
# Override only the optimization level from the CLI:
mlang pkg build -O3
mlang pkg build -Og
mlang pkg build -Os
mlang pkg build -Oz
mlang pkg clean
```

Workspace example:

- `examples/package_manager_workspace_fetch`
  Demonstrates recursive workspace member discovery plus GitHub `git` and
  `tar.gz` source fetching in sibling subpackages.
- `examples/package_manager_static_cjson`
  Demonstrates static linking of a fetched `tar.gz` C dependency.
- `examples/package_manager_multi_bins`
  Demonstrates `[[bin]]` targets, target-scoped build config overrides, and
  mixed GitHub `git` plus `tar.gz` source dependencies in one package.
- `examples/package_manager_multilanguage_example`
  Demonstrates a task-driven single-binary build that compiles MLang, C, and
  C++ sources in separate phases, fetches `miniaudio` and `AudioFile`, and
  links the results together.
- `examples/package_manager_linux_aarch64_qemu`
  Demonstrates a fetch-only Linux kernel dependency plus `[[task]]` commands
  for AArch64 kernel build and QEMU boot flow.
### Inline asm target architecture

Inline asm can be pinned to a target architecture directly in source:

```mla
let sum: i64 = asm aarch64(i64, "add $0, $1, $2", base, delta);
asm volatile aarch64(void, "yield");
```

Plain quoted strings can also span source lines directly. Newlines inside the
literal become `\n` in the resulting value, so asm templates can be written as
real multiline blocks instead of `\n`-escaped single lines. `mlang-format`
preserves the rows inside these multiline string literals so asm examples stay
aligned.

For embedded string/data examples, LLVM inline asm uses directives such as
`.asciz` and `.p2align` rather than NASM-style `db`.

Supported architecture names are `x86`, `x64`, and `aarch64`. The compiler
target can be selected with `--target-arch`:

```bash
mlang --target-arch aarch64 examples/inline_asm_aarch64_demo.mla -L build -lmlang_std -o /tmp/inline_asm_aarch64_demo
mlang --target-arch aarch64 examples/inline_asm_aarch64_hello_demo.mla -L build -lmlang_std -o /tmp/inline_asm_aarch64_hello_demo
mlang --target-arch aarch64 examples/inline_asm_aarch64_data_hello_demo.mla -L build -lmlang_std -o /tmp/inline_asm_aarch64_data_hello_demo
mlang --target-arch x64 -emit-llvm examples/inline_asm_x64_demo.mla -L build -lmlang_std -o /tmp/inline_asm_x64_demo.ll
mlang --target-arch x64 -emit-llvm examples/inline_asm_x64_hello_demo.mla -L build -lmlang_std -o /tmp/inline_asm_x64_hello_demo.ll
mlang --target-arch x64 -emit-llvm examples/inline_asm_x64_data_hello_demo.mla -L build -lmlang_std -o /tmp/inline_asm_x64_data_hello_demo.ll
mlang tools/mlang-frontend-mla/main.mla -L build -lmlang_std -o /tmp/mlang-frontend-mla
/tmp/mlang-frontend-mla --backend mlang --target-arch aarch64 examples/inline_asm_aarch64_demo.mla -L build -lmlang_std -o /tmp/inline_asm_aarch64_demo_frontend
```

If an asm block is tagged for the wrong architecture, compilation fails with an
explicit error before code generation continues.

On an Apple Silicon Mac, prefer the `aarch64` example above for local compile
and run. For non-host architectures such as `x64`, use `-emit-llvm` or `-S`
locally unless you also have the matching toolchain and runtime available.

Benchmark commands:

```bash
mlang bench tests/inline_asm_bench_tests.mla --bench-iters 200000 --bench-warmup 20000 -L build -lmlang_std
mlang --tests tests/inline_asm_target_arch_tests.mla -L build -lmlang_std
mlang --tests tests/multiline_string_tests.mla -L build -lmlang_std -o /tmp/multiline_string_tests_bin && /tmp/multiline_string_tests_bin
```
