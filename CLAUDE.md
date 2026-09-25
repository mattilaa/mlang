# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`mlang` is the compiler for the MLang language (`.mla` files), plus its stdlib, package manager, formatter, LSP servers, and a few audio apps written in the language itself. The full README (`README.md`, ~115KB) and `docs/` are the detailed reference; the GitHub wiki under `docs/wiki/` is generated from `docs/` by `scripts/generate_github_wiki.py`, so edit `docs/`, not the wiki copies.

## Build

The toolchain is **self-hosted in stages**. A C++ "seed compiler" (`mlang`) and its static runtime (`libmlang_std.a`) are built with CMake; the MLang-native tools (`mlangd-mla`, `mlang-format`, `mlang-frontend-mla`, `mlangpkg`) are then compiled from `.mla` sources by that seed compiler. There is no prebuilt compiler.

Requirements: CMake, LLVM (CI uses 17), flex, bison, OpenSSL, zstd, z3, rapidjson, python3.

```sh
./bootstrap.sh              # CMake-builds mlang, mlang_std, mlang-config
./build.sh --install        # rebuilds seed, then compiles the .mla tools (build.ps1/bootstrap.ps1 on Windows)

# Seed compiler + runtime only (fast iteration on src/ or stdlib/src/):
cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mlang mlang_std -j 8

# Self-hosted tools via the bootstrap manifest (task graph run by `mlang pkg`):
build/mlang pkg --config bootstrap/mlang.toml run build-all
build/mlang pkg --config bootstrap/mlang.toml run build-all --tasks   # dry-run listing
```

Outputs land in `build/`. `MLANG_STDLIB_LIB_PATH`, `MLANG_STDLIB_PATH`, and `MLANG_MODULE_PATH` control where compiled programs find `libmlang_std.a`, `stdlib/std/*.mla`, and `modules/*` (dsp, tui). Source-tree builds discover these automatically; programs built by hand need `-L build -lmlang_std`.

On macOS, bootstrap tasks codesign the binaries they produce (`sign = [...]` on tasks, `--sign`/`--force-sign` flags; see README "Codesigning Built Binaries On macOS").

## Testing

Tests are `#[test]` functions in `.mla` files (return `0` = pass), run by the compiler itself:

```sh
build/mlang test                                   # tests/ directory
build/mlang --tests tests/std_math_tests.mla       # one file
build/mlang --tests tests/ --filter "addition"     # filter by test name
build/mlang bench tests/bench_stdlib.mla           # bench_* files
```

Other layers (all need `-DBUILD_TESTS=ON`, the CMake default):
- **GoogleTest** (`tests/mla_tests.cpp`, fetched via FetchContent): compiler-level tests; `./tests/run_tests.sh -j 8` or `ctest -L cpp` / `--target run_cpp_tests`. Single test: `ctest -R <name>` or `build/tests/mla_tests --gtest_filter=...`.
- **Robot Framework** (`tests/robot/`, needs `.venv` + `tests/requirements.txt`): compiles every example with `mlang -c`; `./tests/run_examples_robot.sh`. Examples are listed explicitly in the `.robot` files, so a new `examples/*.mla` must be added there to be covered.
- **LSP transcript tests** (`tests/lsp_*.py`): JSON-RPC scripts run against a built server, e.g. `python3 tests/lsp_parity_e2e.py --mlangd build/mlangd`, `python3 tests/lsp_mlangd-mla_rename_transcript.py --mlangd build/mlangd-mla`.
- `scripts/run_asan.sh` for AddressSanitizer runs.

## Architecture

**Compiler pipeline** (`src/`): `lexer.l` (flex) → `parser.y` (bison) → AST (`ast.cpp`, `include/ast*.h`) → LLVM IR generation in `src/ir/` → object/executable. `src/main.cpp` is the driver and also routes `test`, `bench`, `pkg`, etc. `src/ir/` is split by concern (one file per area: `struct_methods.cpp`, `function_calls.cpp`, `control_flow.cpp`, `borrows.cpp`/`moves.cpp`/`ownership.cpp` for the ownership model, `monomorphization.cpp` for generics, `constexpr.cpp` for `cexpr`, `testing.cpp` for `#[test]`, `return_inference.cpp`, etc.). `incremental_compiler.cpp`/`ide_query.cpp`/`compiler_api.cpp` expose the front half of the compiler as an incremental session API for tooling (surfaced to MLang as `std::compiler`).

**Generated code — do not hand-edit:** `parser.cpp/.hpp` and `lexer.cpp` are generated into the build dir by bison/flex from `src/parser.y` / `src/lexer.l`. `include/ast_impl.h`, `src/ast.cpp`, and `src/ast_bridge.cpp` are produced by `scripts/generate_ast_bridge.py`, which scrapes `create_*/add_*/set_*` `ASTNode*` declarations from `include/ast.h` and `src/parser.y` — after changing those signatures, re-run the script (from the repo root) and commit its output.

**Stdlib** has two halves that must stay in sync: `stdlib/std/**/*.mla` are the MLang module sources (and the editor-visible API), backed by native runtime code in `stdlib/src/*.{c,cpp}` that is compiled into `libmlang_std.a`. Adding or changing a stdlib function usually means touching both, plus `docs/stdlib/std_<name>.md` and a `tests/std_<name>_tests.mla`. `stdlib/{types,macros,attributes,test}.mla` describe compiler built-ins for tooling. Libraries outside the `std` namespace live in `modules/` (`dsp`, `tui`, `tui_demo`) and are imported as `dsp::filter` etc.

**Tools** (`tools/`): `mlang_config.cpp` and `tools/mlang_lsp_cpp` (legacy C++ `mlangd`) are CMake-built; `mlangd-mla`, `mlang-format-mla`, `mlang-frontend-mla`, `mlang-pkg-mla`, `mlangpkg` are written in MLang. `mlang pkg` is the package manager (implementation in `src/package_manager.cpp`; `MLANG_PKG_IMPL` selects the MLang frontend vs the C++ one). Projects are described by `mlang.toml` with `[[task]]` blocks (`depends_on`, `commands`, `sign`); `bootstrap/mlang.toml` is the canonical example.

**Apps on top of the toolchain**, each a standalone `mlang.toml` project built with `build/mlang pkg --config <dir>/mlang.toml build` (they need `build/libmlang_std.a` first, and fetch the pinned VST3 SDK):
- `mlacker/` — terminal tracker (AUHAL output, VST3 host, `.mlack` sessions); shares UI code with `modules/tui_demo`. Run: `build/mlang pkg --config mlacker/mlang.toml run run`.
- `plugins/mla_verb`, `plugins/mla_distortion`, `plugins/mla_delay`, `plugins/mla_eq`, `plugins/mla_filter`, `plugins/mla_stutter`, `plugins/mla_juno_chorus` — VST3 effects: DSP in MLang (`modules/dsp/*`), thin C++ `plugin.cpp` wrapper. Output bundle lands in `<plugin>/build/cmake/VST3/`.

## Conventions

- MLang code is formatted with `mlang-format` using `.mlang-format` (Rust-style, 4-space indent, 100 cols, no forced trailing newline); C++ uses `.clang-format`.
- `build/`, `*_commands.json`, `*bin`, `*.out`, and `*html`/`*xml` are gitignored. `mlang.lock` files are committed for `mlacker/` (the plugins' are currently untracked). Stray binaries in the repo root (`a.out`, `mlang_*_bin`) and `.vst3` bundles are build artifacts, not sources.
- CMake auto-increments a build number on every configure (`MLANG_AUTO_INCREMENT_BUILD`); CI overrides it with `-DMLANG_VERSION_BUILD`.
