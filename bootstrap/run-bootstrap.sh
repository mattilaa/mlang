#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
manifest="${script_dir}/mlang.toml"
default_build_dir="${repo_root}/build"
config_file="${default_build_dir}/mlang-config.conf"

config_value() {
  local key="$1"
  local file="$2"
  [ -f "$file" ] || return 0
  awk -F= -v key="$key" '$1 == key { sub(/^[ \t]+/, "", $2); sub(/[ \t]+$/, "", $2); print $2; exit }' "$file"
}

has_option_override() {
  local key="$1"
  shift
  local prev=""
  for arg in "$@"; do
    if [ "$prev" = "--option" ]; then
      case "$arg" in
        "$key="*) return 0 ;;
      esac
    fi
    case "$arg" in
      "--option=$key="*) return 0 ;;
    esac
    prev="$arg"
  done
  return 1
}

append_config_option() {
  local option_key="$1"
  local config_key="$2"
  shift 2
  if has_option_override "$option_key" "$@"; then
    return
  fi
  local value
  value="$(config_value "$config_key" "$config_file")"
  if [ -n "$value" ]; then
    args+=("--option" "$option_key=$value")
  fi
}

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
  ./bootstrap/run-bootstrap.sh config
  ./bootstrap/run-bootstrap.sh run build-all --tasks
  ./bootstrap/run-bootstrap.sh run build-all
  ./bootstrap/run-bootstrap.sh run build-mlangd
  ./bootstrap/run-bootstrap.sh run install-mlangd
  ./bootstrap/run-bootstrap.sh run install-tooling

You can also point the launcher at a specific compiler:
  MLANG_BOOTSTRAP_BIN=/path/to/mlang ./bootstrap/run-bootstrap.sh run build-all
EOF
}

if [[ "${1:-}" == "config" ]]; then
  shift
  cmake -S "${repo_root}" -B "${default_build_dir}" -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build "${default_build_dir}" --target mlang-config
  exec "${default_build_dir}/mlang-config" "$@"
fi

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

args=("$@")
if [[ "${args[0]:-}" == "run" && -f "$config_file" ]]; then
  append_config_option "install_prefix" "install_prefix" "$@"
  append_config_option "bin_dir" "bin_dir" "$@"
  append_config_option "cmake_build_type" "build_type" "$@"
fi

exec "${bootstrap_bin}" pkg --config "${manifest}" "${args[@]}"
