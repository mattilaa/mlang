# Selected Stdlib And Test Modules {#stdlib_misc_modules}

This page documents several small stdlib modules and example/test files that
are useful in practice but do not have their own larger standalone reference
pages.

## `std::chat`

`std::chat` provides an opaque, handle-backed fullscreen chat UI model for
terminal applications.

What it is for:

- line-oriented terminal chat interfaces
- status/header/prompt rendering
- bounded scrollback with input submission handling

Typical flow:

1. `ChatUi::new(max_lines)`
2. set title/server/channel/nick/prompt fields
3. push chat lines and feed keycodes
4. render with `render(rows, cols)`

Source:

- `stdlib/std/chat.mla`

## `std::concurrent`

`std::concurrent` adds higher-level coordination helpers on top of
`std::sync` and `std::thread`.

Main types:

- `WaitGroup`
  Count-down style completion tracking for multiple workers and one waiter.
- `OrderedGate`
  Deterministic turn-based gate for staged multithreaded execution.

Source:

- `stdlib/std/concurrent.mla`

## `std::env`

`std::env` exposes runtime-backed process/environment helpers.

Common functions:

- `args()`
- `cwd()`
- `get(name)`
- `set(name, value)`
- `unset(name)`
- `println(msg)`
- `eprintln(msg)`

It also includes `HelpOption` and helper functions for rendering command help.

Source:

- `stdlib/std/env.mla`

## `std::gps`

`std::gps` converts latitude/longitude data into local integer XY coordinates.

Useful functions:

- `deg_to_rad`
- `project_x`, `project_y`
- `project_xs`, `project_ys`
- `distance_m`

This module is intended for routing, map projection, and TSP-style examples.

Source:

- `stdlib/std/gps.mla`

## `std::path`

`std::path` provides lightweight path helpers used by tooling and package
manager code.

Common functions:

- `join`
- `basename`
- `dirname`
- `normalize`

Compatibility aliases are also exported:

- `path_join`
- `path_normalize`
- `path_basename`
- `path_dirname`

Source:

- `stdlib/std/path.mla`

## `std::sed`

`std::sed` provides literal, allocation-returning string replacement helpers.

Common functions:

- `replace_first`
- `replace_all`
- `substitute`

Matching is literal and byte-based, not regex-based.

Source:

- `stdlib/std/sed.mla`

## Test And Benchmark Examples

### `tests/test_sample.mla`

Basic `mlang test` example using `std::testing::verify`.

Run:

```sh
./build/mlang test tests/test_sample.mla -L ./build -lmlang_std
./build/mlang test tests/test_sample.mla --filter addition -L ./build -lmlang_std
```

### `tests/bench_stdlib.mla`

Benchmark-oriented test file for `mlang bench`.

It demonstrates:

- timed benchmark tests
- `std::bench` clobber/do-not-optimize helpers
- container and FFT benchmark coverage
- exception/unwind benchmark cases

Run:

```sh
./build/mlang bench tests/bench_stdlib.mla -L ./build -lmlang_std
./build/mlang bench tests/bench_stdlib.mla --bench-iters 50000 -L ./build -lmlang_std
```

### `examples/testing_mock_example.mla`

Simple mock-based example built on `std::testing`.

It demonstrates:

- creating a mock
- declaring expected calls
- marking calls as observed
- verifying the expectations

Run:

```sh
./build/mlang examples/testing_mock_example.mla -o /tmp/testing_mock_example
/tmp/testing_mock_example
```

### `examples/test_fixture_example.mla`

`#[fixture]` impl example. Each `#[test]` method runs against a fresh,
stack-allocated, zero-initialized instance of the fixture struct (similar
to GoogleTest `TEST_F`).

It demonstrates:

- `#[fixture]` on an `impl` block
- `setup(self: &mut Self)` running before every test
- `teardown(self: &mut Self)` running after every test
- per-test isolation (mutations in one test don't bleed into the next)
- mixing `void` and `i32`-returning test methods

Run:

```sh
./build/mlang --tests examples/test_fixture_example.mla
```

### `examples/expect_call_example.mla`

EXPECT_CALL-style mock example. Builds on `examples/testing_mock_example.mla`
with cardinality variants and programmable return values.

It demonstrates:

- `mock_expect_times` (exact count) / `mock_expect_at_least`
  / `mock_expect_at_most` / `mock_expect_never`
- queueing typed return values with `mock_will_return_i32` /
  `mock_will_return_bool` (and i64/str8/f32/f64 variants)
- writing a mock body with `mock_record_and_return_*`, which records the
  invocation AND returns the next queued value (or a default fallback)
- driving a "system under test" through the mocked interface

Run:

```sh
./build/mlang examples/expect_call_example.mla -o /tmp/expect_call_example
/tmp/expect_call_example
```
