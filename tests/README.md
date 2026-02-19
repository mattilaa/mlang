# MLA Language Test Suite

This directory contains the Google Test-based test suite for the MLA programming language compiler.

## Directory Structure

```
mla/
├── CMakeLists.txt          # Main project CMake (includes tests)
├── src/                    # Compiler source files
├── include/                # Header files
├── tests/                  # Test suite (this directory)
│   ├── CMakeLists.txt      # Test CMake configuration
│   ├── mla_tests.cpp       # Test source file
│   ├── run_tests.sh        # Convenience script
│   └── README.md           # This file
└── build/                  # Build output (created by CMake)
```

## Prerequisites

- CMake 3.20 or higher
- C++17 compatible compiler (GCC, Clang, MSVC)
- LLVM development libraries
- Flex and Bison
- Internet connection (for downloading Google Test on first build)

## Building and Running Tests

### Option 1: Using the Script (Recommended)

From the project root:
```bash
./tests/run_tests.sh
```

Or from the tests directory:
```bash
cd tests
./run_tests.sh
```

### Option 1b: Compile All Examples (Robot Framework)

This uses Robot Framework to compile every example with `mlang -c`.

Setup (virtual env + deps):

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r tests/requirements.txt
```

```bash
./tests/run_examples_robot.sh
```

### Option 1c: LSP End-to-End Transcript Test

This runs a JSON-RPC integration script against `mlangd --stdio` and checks:
- `textDocument/implementation`
- `textDocument/references`
- rename safety (`textDocument/rename` blocked on unsafe rename)
- `codeAction` organize imports
- range formatting hook endpoint (`textDocument/rangeFormatting`)

```bash
python3 tests/lsp_integration_transcript.py --mlangd build/mlangd
```

### Option 1d: LSP Parity End-to-End (All Methods + Large Workspace)

This runs a broader JSON-RPC parity suite against `mlangd --stdio`, covering:
- all currently advertised request methods (`definition`, `implementation`,
  `references`, `hover`, `documentHighlight`, `completion`, `signatureHelp`,
  `prepareRename`, `rename`, `documentSymbol`, `formatting`,
  `rangeFormatting`, `codeAction`, `diagnostic`, `semanticTokens/full`,
  `workspace/symbol`)
- notification lifecycle (`didOpen`, `didChange`, `didSave`, `didClose`)
- large workspace scan scenario (hundreds of `.mla` files)
- module resolution via `mlang.toml` `module_paths`

```bash
python3 tests/lsp_parity_e2e.py --mlangd build/mlangd
# Optional: tune large workspace size
python3 tests/lsp_parity_e2e.py --mlangd build/mlangd --bulk-files 400
```

### Option 1e: mlangd_mla organizeImports Transcript (CodeAction)

This is a focused end-to-end JSON-RPC transcript for `tools/mlangd_mla` that
locks:
- `textDocument/codeAction` with `source.organizeImports`
- state updates across `didOpen` / `didChange` / `didClose`

```bash
./build/mlang tools/mlangd_mla/main.mla -L ./build -lmlang_std -o /tmp/mlangd_mla
python3 tests/lsp_mlangd_mla_codeaction_transcript.py --mlangd /tmp/mlangd_mla
```

### Option 1f: mlangd_mla Rename Transcript (WorkspaceEdit documentChanges)

This focused end-to-end JSON-RPC transcript for `tools/mlangd_mla` locks:
- `textDocument/rename` returns `WorkspaceEdit.documentChanges` (not legacy `changes`)
- rename edits include both definition and cross-document references

```bash
./build/mlang tools/mlangd_mla/main.mla -L ./build -lmlang_std -o /tmp/mlangd_mla
python3 tests/lsp_mlangd_mla_rename_transcript.py --mlangd /tmp/mlangd_mla
```

### Option 1g: mlangd_mla Transcript Suite (Consolidated Runner)

Runs both focused `mlangd_mla` transcript checks in one command:
- organizeImports codeAction lifecycle
- rename `documentChanges` + cross-document edits

```bash
./build/mlang tools/mlangd_mla/main.mla -L ./build -lmlang_std -o /tmp/mlangd_mla
python3 tests/lsp_mlangd_mla_transcripts.py --mlangd /tmp/mlangd_mla
```

### Option 2: Manual CMake Build

```bash
mkdir -p build
cd build
cmake -DBUILD_TESTS=ON ..
cmake --build .
ctest --output-on-failure
```

### Option 3: Run Tests Directly

After building:
```bash
./build/tests/mla_tests
```

### Disable Tests

To build without tests:
```bash
cmake -DBUILD_TESTS=OFF ..
```

## Test Categories

The test suite covers the following language features:

### Basic Programs
- Empty main function
- Return values
- Return expressions

### Integer Types
- Signed integers: `i8`, `i16`, `i32`, `i64`
- Unsigned integers: `u8`, `u16`, `u32`, `u64`
- Min/max value tests for each type

### Floating Point
- `float` literals (e.g., `3.14f`)
- `double` literals (e.g., `3.14159`)

### Strings
- String literals
- Escape sequences (`\n`, `\t`, `\"`, `\\`)

### Print Macros
- `println!` - stdout with newline
- `print!` - stdout without newline
- `eprintln!` - stderr with newline
- `eprint!` - stderr without newline
- Format string interpolation with `{}`

### Variables
- `let` declarations (immutable)
- `var` declarations (mutable)
- Reassignment

### Arithmetic
- Addition, subtraction, multiplication, division
- Operator precedence
- Complex expressions with parentheses

### Comparisons
- `<`, `>`, `<=`, `>=`, `==`, `!=`

### Control Flow
- `if` statements
- `else if` chains
- `else` blocks
- Nested conditionals

### Loops
- `for` loops with ranges (`for i in 0..10`)
- Nested loops
- Loop variable accumulation

### Functions
- Function definitions with parameters
- Return values
- Void functions
- Recursive functions (factorial, fibonacci)
- Multiple functions

### Type Casts
- `int()` cast
- `float()` cast
- `double()` cast

### Comments
- Single-line comments (`//`)
- Multi-line comments (`/* */`)

### Integration Tests
- FizzBuzz
- Sum of squares
- Prime number detection

## Adding New Tests

To add a new test, add a `TEST_F` macro in `mla_tests.cpp`:

```cpp
TEST_F(MLATest, MyNewTest)
{
    std::string code = R"(
        fn main() -> i32 {
            // Your MLA code here
            println!("Expected output");
            return 0;
        }
    )";
    EXPECT_EQ(compileAndRun(code), "Expected output\n");
}
```

### Test Helper Methods

| Method | Description |
|--------|-------------|
| `compileAndRun(code)` | Compile and run, return stdout |
| `compileAndRunExitCode(code)` | Compile and run, return exit code |
| `writeSource(code)` | Write source to file |
| `compile()` | Compile the source file |
| `run()` | Run the executable, return stdout |
| `runStderr()` | Run the executable, return stderr |
| `runExitCode()` | Run the executable, return exit code |

## Test Output

Successful run:
```
[==========] Running 50 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 50 tests from MLATest
[ RUN      ] MLATest.EmptyMain
[       OK ] MLATest.EmptyMain (150 ms)
...
[----------] 50 tests from MLATest (5000 ms total)
[==========] 50 tests from 1 test suite ran. (5000 ms total)
[  PASSED  ] 50 tests.
```

## Using with CTest

CMake's CTest is integrated:

```bash
cd build
ctest                          # Run all tests
ctest -V                       # Verbose output
ctest -R "Integer"             # Run tests matching "Integer"
ctest --output-on-failure      # Show output only for failed tests
```

## Troubleshooting

### Google Test download fails
Ensure you have internet connectivity for the first build. Google Test is cached after the first download.

### Tests fail to find compiler
The tests automatically find the `mlang` compiler from the build. If running manually, set the `MLA_COMPILER` environment variable:
```bash
MLA_COMPILER=/path/to/mlang ./mla_tests
```

### Tests timing out
Some tests (especially recursive ones) may take longer. Increase timeout if needed:
```bash
ctest --timeout 300
```
