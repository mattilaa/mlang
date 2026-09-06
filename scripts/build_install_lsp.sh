#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: build_install_lsp.sh [--install] [--no-install] [--prefix <path>] [--system] [--sudo] [--build-dir <dir>] [--use-make] [--help]

Builds the LSP targets and optionally installs them.

Options:
  --install          Install after build (default)
  --no-install       Skip install step
  --prefix <path>    Install prefix (default: $HOME/.local)
  --system           Install to /usr/local (implies --sudo)
  --sudo             Use sudo for install step
  --build-dir <dir>  Build directory (default: build)
  --use-make         Use Unix Makefiles instead of Ninja
  --help             Show this help
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
cmake --build "$build_dir" --target mlangd mlangd-mla mlang-config

if $install_after_build; then
  if $use_sudo; then
    sudo cmake --install "$build_dir" --prefix "$prefix"
  else
    cmake --install "$build_dir" --prefix "$prefix"
  fi
fi
