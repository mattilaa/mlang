#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
manifest="${script_dir}/mlang.toml"

bootstrap_bin="${MLANG_BOOTSTRAP_BIN:-}"
if [[ -z "${bootstrap_bin}" ]]; then
  if [[ -x "${repo_root}/build/mlang" ]]; then
    bootstrap_bin="${repo_root}/build/mlang"
  elif command -v mlang >/dev/null 2>&1; then
    bootstrap_bin="$(command -v mlang)"
  else
    cat >&2 <<'EOF'
bootstrap requires an existing `mlang` binary before it can run `mlang pkg`.

Build one from the repository root:
  cmake -S . -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target mlang mlang_std

Optional install step:
  cmake --install build --prefix "$HOME/.local"

Then rerun one of:
  ./bootstrap/run-bootstrap.sh run build-tooling
  ./bootstrap/run-bootstrap.sh run install-tooling
EOF
    exit 1
  fi
fi

exec "${bootstrap_bin}" pkg --config "${manifest}" "$@"
