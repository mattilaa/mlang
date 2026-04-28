# mlang
MLang - Programming Language

## Table Of Contents
- [LSP](#lsp)
- [C++ LSP](#c-lsp)
- [Mlangd (Mlang Scaffold)](#mlangd-mlang-scaffold)
- [Compiler Frontend (Primary MLang CLI)](#compiler-frontend-primary-mlang-cli)
- [Package Manager (MLang Backend Default)](#package-manager-mlang-backend-default)
- [Stdlib Linking](#stdlib-linking)
- [Build + Install](#build--install)
- [Documentation](#documentation)
- [AddressSanitizer Verification](#addresssanitizer-verification)
- [Formatter](#formatter)
- [Quickstart (Package Manager + curl example)](#quickstart-package-manager--curl-example)
- [Testing](#testing)
- [Multithreaded TCP Demo (Local)](#multithreaded-tcp-demo-local)
- [Advanced Protocol Stack Demo (Local)](#advanced-protocol-stack-demo-local)
- [Examples](#examples)
- [Package Manager (C++)](#package-manager-c)
- [Package Workspaces And Fetched Subprojects](#package-workspaces-and-fetched-subprojects)

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
mlang main.mla -L ~/.local/lib -lmlang_std
```

You can also set a default search path:

```sh
export MLANG_STDLIB_LIB_PATH=~/.local/lib
```

The stdlib module search path is controlled by `MLANG_STDLIB_PATH` and defaults
to `~/.local/share/mlang/stdlib` when installed.

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

Once `mlang` is available on `PATH`, build the remaining bootstrap-managed
components either through the helper script:

```sh
./bootstrap/run-bootstrap.sh run build-all
./bootstrap/run-bootstrap.sh run install-tooling
```

or directly with `mlang pkg` if you want to skip the helper:

```sh
mlang pkg --config bootstrap/mlang.toml run build-all
mlang pkg --config bootstrap/mlang.toml run install-tooling
```

You can also run individual steps instead of the whole chain:

```sh
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla
mlang pkg --config bootstrap/mlang.toml run build-mlangd-mla --asan
mlang pkg --config bootstrap/mlang.toml run build-mlang-format
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
- `build-tooling`
- `unit-tests`
- `robot-tests`
- `install-mlang`
- `install-mlangd-mla`
- `install-mlang-format`
- `install-mlang-frontend`
- `install-tooling`

## Documentation

The repository ships a Doxygen-based documentation build. Run the helper from the top level to regenerate HTML under `docs/out`:

```sh
doxygen docs/Doxyfile
```

The generated site mirrors the Markdown sources under `docs/`, stdlib sources
(`stdlib/std/`, `stdlib/src/`), and MLang tool sources
(`tools/mlang-frontend-mla/main.mla`, `tools/mlang-pkg-mla/main.mla`). Open
`docs/out/index.html` in a browser after running the command.

Compiler diagnostic reference lives in `docs/compiler_diagnostics.md` and is
published in the generated site as `docs/out/html/compiler_diagnostics.html`.

## AddressSanitizer Verification
After a clean workspace, run the helper script that configures an AddressSanitizer build and runs the unit and robot test suites under ASan:

```sh
./scripts/run_asan.sh
```

The script delegates to `./scripts/build_install.sh --asan --unit-tests --robot-tests --no-install`, defaults to `build-asan` plus `artifacts-asan`, and propagates `ASAN_OPTIONS` to the compiler, unit tests, and robot runs. You can still override the default sanitizer tuning when invoking the script if you want stricter checks, for example `ASAN_OPTIONS=detect_container_overflow=1:strict_init_order=1 ./scripts/run_asan.sh`.

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
- `.mlang-format`: `IndentWidth`, `EnsureTrailingNewline`,
  `SpaceAfterComma`, `SpaceAfterColon`, `SpaceAroundOperators`,
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
FFT API (`std::algorithm::fft` forward/inverse on split real/imag arrays) example:
`examples/std_fft_demo.mla`.
Regex API (`std::regex::Regex`, compile/match/find/captures) example:
`examples/std_regex_demo.mla`.
Multithreaded TCP server/client examples:
`examples/std_net_mt_server.mla` and `examples/std_net_mt_client.mla`.
Advanced framed protocol stack examples (isolated in subdirectory):
`examples/protocol_mt/server.mla` and `examples/protocol_mt/client.mla`
with runner script `examples/protocol_mt/run_demo.sh`.
JSON API (`std::json::JsonDoc` parse/stringify/object-array navigation, iterators, and `from_file`) example:
`examples/std_json_demo.mla`.
JSON-RPC/LSP transport runtime (`std::jsonrpc` Content-Length framing, timeout reads, cancellation registry, queue runtime) example:
`examples/std_jsonrpc_runtime_demo.mla`.
Manual stdio JSON-RPC worker runtime demo (`run_stdio_loop`, built-in `$/cancelRequest` routing):
`examples/std_jsonrpc_stdio_loop_demo.mla` (manual run, not part of Robot suite).
Incremental parse/query API for tooling (`std::compiler::Session`, open/change/close, diagnostics, hover, completion, document symbols, cross-document definition via `mod` files) example:
`examples/std_compiler_demo.mla`.
`?` is supported for `Result` propagation (early-return on `Err`).

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
- Type aliases (`use type Distance = f32;`, generic aliases):
  `examples/type_alias_demo.mla`
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
analyzer with colorized stereo differences (left/right/overlap) using `std::algorithm::fft`.
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
