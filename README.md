# mlang
MLang - Programming Language

## LSP
Primary LSP servers:
- `build/mlangd` (C++)
- `build/mlangd-mla` (Mlang)

```sh
./build/mlangd --stdio
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

## Compiler Frontend (Primary MLang CLI)
Feature-rich frontend implementation lives at `tools/mlang-frontend-mla/main.mla`.
Current scope:
- parses frontend option `--backend`
- command-dispatch parity for `test`, `run tests`, `bench`, `pkg`
- directory suite mode for `test`/`bench` (runs suites one-by-one)
- stdlib link auto-discovery for compile passthrough (`-L... -lmlang_std -lm`)
- pkg frontend binary caching (skip recompilation when cached tool is up-to-date)

```sh
./build/mlang tools/mlang-frontend-mla/main.mla -L ./build -lmlang_std -o /tmp/mlang-frontend-mla
/tmp/mlang-frontend-mla --backend ./build/mlang --help
/tmp/mlang-frontend-mla --backend ./build/mlang examples/main.mla -o /tmp/main_bin
/tmp/mlang-frontend-mla --backend ./build/mlang test tests
/tmp/mlang-frontend-mla --backend ./build/mlang run tests tests
/tmp/mlang-frontend-mla --backend ./build/mlang bench tests --bench-iters 200 --bench-warmup 20
```

You can route `mlang` itself through the MLang frontend implementation:

```sh
MLANG_FRONTEND_IMPL=mla ./build/mlang examples/main.mla -o /tmp/main_bin
```

After install (`./scripts/build_install.sh`), prefer:

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

## Package Manager (MLang Backend Default)
`mlang pkg ...` now prefers the MLang implementation in
`tools/mlang-pkg-mla/main.mla` by default, with automatic fallback to the C++
backend if needed.

Force backend selection with:

```sh
MLANG_PKG_IMPL=mla ./build/mlang pkg init
MLANG_PKG_IMPL=cpp ./build/mlang pkg init
```

Build and run:

```sh
./build/mlang tools/mlang-pkg-mla/main.mla -L ./build -lmlang_std -o /tmp/mlang-pkg-mla
/tmp/mlang-pkg-mla --backend ./build/mlang init
/tmp/mlang-pkg-mla --backend ./build/mlang add cjson --git https://github.com/DaveGamble/cJSON.git
/tmp/mlang-pkg-mla --backend ./build/mlang fetch
/tmp/mlang-pkg-mla --backend ./build/mlang build -O2
# Optional for CMake-based deps:
/tmp/mlang-pkg-mla --backend ./build/mlang build -O2 --ninja
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
Build and install compiler + tools (`mlang`, `mlangd`, `mlangd-mla`,
`mlang-format`, `mlang-frontend-mla`, `mlang-frontend`):

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

## Documentation

The repository ships a Doxygen-based documentation build. Run the helper from the top level to regenerate HTML under `docs/out`:

```sh
doxygen docs/Doxyfile
```

The generated site mirrors the Markdown sources under `docs/`, stdlib sources
(`stdlib/std/`, `stdlib/src/`), and MLang tool sources
(`tools/mlang-frontend-mla/main.mla`, `tools/mlang-pkg-mla/main.mla`). Open
`docs/out/index.html` in a browser after running the command.

## AddressSanitizer Verification
After a clean workspace, run the helper script that configures an AddressSanitizer build, compiles the stdlib, and exercises `mlang` on a representative input so you can confirm the compiler no longer crashes under ASan:

```sh
./scripts/run_asan.sh
```

The script wipes `build-asan`, configures CMake with the required `-fsanitize=address` flags, builds the project with Ninja, and then runs `mlang --version` followed by a smoke `-c tools/mlang-compiler-mla/ast.mla` compilation. You can override the default sanitizer tuning with `ASAN_OPTIONS` when invoking the script if you want stricter checks (e.g., `ASAN_OPTIONS=detect_container_overflow=1:strict_init_order=1 ./scripts/run_asan.sh`).

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
  `SpaceAroundRelationalOperators`, `SpaceInsideBracesSingleLine`,
  `CompactFatArrow` (default: `true`)

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
# Or: ../../build/mlang pkg build -O3 --ninja
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
Trait-like `std::io` handles (`Read`, `Write`, `Seek`, `BufRead`) example:
`examples/std_io_traits_demo.mla`.
Filesystem API (`std::fs::File`, `std::fs::BufReader`) example:
`examples/std_fs_demo.mla`.
TCP networking API (`std::net::TcpListener`, `std::net::TcpStream`,
non-blocking mode, read/write timeouts) example:
`examples/std_net_demo.mla`.
Random API (`std::rand` seed/range helpers) example:
`examples/std_rand_demo.mla`.
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
./build/mlang examples/std_net_mt_server.mla -o /tmp/std_net_mt_server_bin
/tmp/std_net_mt_server_bin --port 18788

# Terminal 2: run client
./build/mlang examples/std_net_mt_client.mla -o /tmp/std_net_mt_client_bin
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
./build/mlang examples/protocol_mt/server.mla -o /tmp/protocol_mt_server
/tmp/protocol_mt_server --port 19095 --clients 4 --rounds 3

# Terminal 2
./build/mlang examples/protocol_mt/client.mla -o /tmp/protocol_mt_client
/tmp/protocol_mt_client --port 19095 --clients 1 --rounds 7 --delay-min-ms 500 --delay-max-ms 1000
```

Run the Robot Framework example suite (includes the multithreaded net case):

```sh
./tests/run_examples_robot.sh
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
./build/mlang examples/jack2_lockfree_thread_demo.mla -L /tmp -ljack2_mlang_bridge $(pkg-config --libs jack) -o /tmp/jack2_lockfree_demo
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
./build/mlang examples/sampler_example/jack2_drum_machine.mla -L /tmp -ljack2_drum_machine $(pkg-config --libs jack) -o /tmp/jack2_drum_machine_demo
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
