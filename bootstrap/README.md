# Bootstrap Tasks

This directory replaces the old `scripts/build_install.sh` flow with
`mlang pkg` tasks.

Use the launcher:

```sh
./bootstrap/run-bootstrap.sh run <task>
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
./bootstrap/run-bootstrap.sh run install-tooling
```

Notes:

- Each task is intentionally small so you can build or test one step at a
  time instead of always running the full installer.
- Install defaults come from `[tool.mlang.options]`:
  `install_prefix = "$HOME/.local"` and `bin_dir = "$HOME/.local/bin"`.
- Override them on one command line with
  `mlang pkg --config bootstrap/mlang.toml run build-and-install --option install_prefix=$HOME/.local --option bin_dir=$HOME/.local/bin`.
- `--asan` also applies to `build-mlangd-mla`, `build-mlang-format`, and
  `build-all`.
- If `mlang` is not yet installed, `run-bootstrap.sh` first checks
  `./build/mlang` and then `PATH`.
