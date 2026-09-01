# Bootstrap Tasks

This directory replaces the old `scripts/build_install.sh` flow with
`mlang pkg` tasks.

Use the launcher:

```sh
./bootstrap/run-bootstrap.sh run <task>
```

Prerequisite: the launcher delegates to `mlang pkg`, so a clean checkout must
first build a seed compiler:

```sh
cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mlang mlang_std
```

Or build and install it to `~/.local/bin` in one line:

```sh
cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release; cmake --build build --target mlang mlang_std; cmake --install build --prefix "$HOME/.local" --component mlang-core
```

That install step also installs native man pages such as `man mlang`,
`man mlang-pkg`, `man mlangd-mla`, and `man mlang-format` under
`$HOME/.local/share/man/man1`.

The repository also ships checked-in roff sources under `docs/man/`. Stage
them into the build tree with:

```sh
cmake --build build --target manpages
```

Preview one directly with:

```sh
man -l docs/man/mlang.1
```

After that, `run-bootstrap.sh` uses `./build/mlang` automatically. You can also
install `mlang` on `PATH` or set `MLANG_BOOTSTRAP_BIN=/path/to/mlang`.

## Config Menu

`mlang-config` is a small menu tool for common bootstrap settings. It does not
disable language features; MLang language behavior is always built in. It only
selects workflow paths and optional test runs. Unit tests and Robot tests
default to `OFF`.

Build and run it through bootstrap:

```sh
./bootstrap/run-bootstrap.sh config
```

The menu writes:

- `build/mlang-config.conf` for bootstrap defaults.
- `build/mlang_config_cache.cmake` for direct CMake use.

After saving, normal bootstrap commands automatically import saved
`install_prefix`, `bin_dir`, and `build_type` unless you pass explicit
`--option` overrides:

```sh
./bootstrap/run-bootstrap.sh run install-tooling
```

Non-interactive config generation is also supported:

```sh
./build/mlang-config --install-prefix ~/.local --bin-dir ~/.local/bin --unit-tests off --robot-tests off --write
```

Current task entrypoints:

- `configure`
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

Examples:

```sh
./bootstrap/run-bootstrap.sh run build-mlang
./bootstrap/run-bootstrap.sh run build-mlangd-mla
./bootstrap/run-bootstrap.sh run build-mlang-format
./bootstrap/run-bootstrap.sh run build-mlang-frontend
./bootstrap/run-bootstrap.sh run build-all
./bootstrap/run-bootstrap.sh run build-all --asan
./bootstrap/run-bootstrap.sh run build-and-install
./bootstrap/run-bootstrap.sh run unit-tests
./bootstrap/run-bootstrap.sh run robot-tests
./bootstrap/run-bootstrap.sh run docs
./bootstrap/run-bootstrap.sh run install-mlangd-mla
./bootstrap/run-bootstrap.sh run install-tooling
```

Notes:

- Each task is intentionally small so you can build or test one step at a
  time instead of always running the full installer.
- Install defaults come from `[tool.mlang.options]`:
  `install_prefix = "$HOME/.local"` and `bin_dir = "$HOME/.local/bin"`.
- Bootstrap installs also copy man pages for `mlangd-mla`,
  `mlang-frontend`, and `mlang-frontend-mla` into
  `$install_prefix/share/man/man1`.
- Override them on one command line with
  `mlang pkg --config bootstrap/mlang.toml run build-and-install --option install_prefix=$HOME/.local --option bin_dir=$HOME/.local/bin`.
- `--asan` also applies to `build-mlangd-mla`, `build-mlang-format`, and
  `build-all`.
- If `mlang` is not yet installed, `run-bootstrap.sh` first checks
  `./build/mlang` and then `PATH`.
