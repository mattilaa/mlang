#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: build_install.sh [--install] [--no-install] [--prefix <path>] [--system] [--sudo] [--build-dir <dir>] [--use-make] [--help]

Builds the mlang compiler and mlangd (C++ LSP) and optionally installs them.

Options:
  --install          Install after build (default)
  --no-install       Skip install step
  --prefix <path>    Install prefix (default: $HOME/.local)
  --system           Install to /usr/local (implies --sudo)
  --sudo             Use sudo for install step
  --build-dir <dir>  Build directory (default: build)
  --use-make         Use Unix Makefiles instead of Ninja
  --help             Show this help

Notes:
  The install step also installs the stdlib docs (including builtin types)
  to: <prefix>/share/mlang/stdlib
USAGE
}

install_after_build=true
prefix="${HOME}/.local"
use_sudo=false
build_dir="build"
generator="Ninja"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    --install)
      install_after_build=true
      shift
      ;;
    --no-install)
      install_after_build=false
      shift
      ;;
    --prefix)
      if [[ $# -lt 2 ]]; then
        echo "error: --prefix requires a value" >&2
        exit 1
      fi
      prefix="$2"
      shift 2
      ;;
    --system)
      prefix="/usr/local"
      use_sudo=true
      shift
      ;;
    --sudo)
      use_sudo=true
      shift
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "error: --build-dir requires a value" >&2
        exit 1
      fi
      build_dir="$2"
      shift 2
      ;;
    --use-make)
      generator="Unix Makefiles"
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

cmake -S . -B "$build_dir" -G "$generator" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build "$build_dir" --target mlang mlangd

if $install_after_build; then
  if $use_sudo; then
    sudo cmake --install "$build_dir" --prefix "$prefix"
  else
    cmake --install "$build_dir" --prefix "$prefix"
  fi

  stdlib_dir="$prefix/share/mlang/stdlib"
  if [[ -f "$stdlib_dir/types.mla" ]]; then
    echo "installed stdlib docs: $stdlib_dir"
  else
    echo "warning: stdlib docs not found at $stdlib_dir" >&2
    echo "         ensure stdlib/ is present in the repo and install again" >&2
  fi
else
  echo "note: stdlib docs (builtin type definitions) are not installed" >&2
  echo "      run with --install to copy stdlib to <prefix>/share/mlang/stdlib" >&2
fi
