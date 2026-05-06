#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
manifest="${script_dir}/mlang.toml"

print_bootstrap_binary_help() {
  cat >&2 <<'EOF'
bootstrap requires an existing `mlang` binary before it can run `mlang pkg`.

In a clean checkout, first build the seed compiler from the repository root:
  cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target mlang mlang_std

After that, this launcher will use:
  ./build/mlang

Optional install step, if you want `mlang` available on PATH:
  cmake --install build --prefix "$HOME/.local"
  export PATH="$HOME/.local/bin:$PATH"

Then rerun, for example:
  ./bootstrap/run-bootstrap.sh run build-all --tasks
  ./bootstrap/run-bootstrap.sh run build-all
  ./bootstrap/run-bootstrap.sh run build-mlangd
  ./bootstrap/run-bootstrap.sh run install-mlangd
  ./bootstrap/run-bootstrap.sh run install-tooling

You can also point the launcher at a specific compiler:
  MLANG_BOOTSTRAP_BIN=/path/to/mlang ./bootstrap/run-bootstrap.sh run build-all
EOF
}

bootstrap_bin="${MLANG_BOOTSTRAP_BIN:-}"
if [[ -n "${bootstrap_bin}" ]]; then
  if [[ ! -x "${bootstrap_bin}" ]]; then
    echo "MLANG_BOOTSTRAP_BIN does not point to an executable: ${bootstrap_bin}" >&2
    echo >&2
    print_bootstrap_binary_help
    exit 1
  fi
else
  if [[ -x "${repo_root}/build/mlang" ]]; then
    bootstrap_bin="${repo_root}/build/mlang"
  elif command -v mlang >/dev/null 2>&1; then
    bootstrap_bin="$(command -v mlang)"
  else
    print_bootstrap_binary_help
    exit 1
  fi
fi

exec "${bootstrap_bin}" pkg --config "${manifest}" "$@"
