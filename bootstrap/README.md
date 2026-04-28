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
- `build-tooling`
- `unit-tests`
- `robot-tests`
- `install-mlang`
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
./bootstrap/run-bootstrap.sh run unit-tests
./bootstrap/run-bootstrap.sh run robot-tests
./bootstrap/run-bootstrap.sh run install-tooling
```

Notes:

- Each task is intentionally small so you can build or test one step at a
  time instead of always running the full installer.
- `install-*` tasks default to `"$HOME/.local"`.
- Override the install prefix with `MLANG_BOOTSTRAP_PREFIX=/path`.
- Override the binary directory with `MLANG_BOOTSTRAP_BIN_DIR=/path/to/bin`.
- If `mlang` is not yet installed, `run-bootstrap.sh` first checks
  `./build/mlang` and then `PATH`.
