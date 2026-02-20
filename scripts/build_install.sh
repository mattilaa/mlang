#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: build_install.sh [--install] [--no-install] [--prefix <path>] [--bin-dir <path>] [--system] [--sudo] [--build-dir <dir>] [--use-make] [--all] [--help]
                        [--unit-tests] [--robot-tests] [--tests] [--no-tests]
                        [--install-if-tests-pass]

Builds the mlang compiler and mlangd (C++ LSP) and optionally installs them.

Options:
  --install          Install after build (default)
  --no-install       Skip install step
  --prefix <path>    Install prefix (default: $HOME/.local)
  --bin-dir <path>   Binary install dir (default: <prefix>/bin, i.e. $HOME/.local/bin)
  --system           Install to /usr/local (implies --sudo)
  --sudo             Use sudo for install step
  --build-dir <dir>  Build directory (default: build)
  --use-make         Use Unix Makefiles instead of Ninja
  --all              Build/install both mlang and mlangd (default behavior)
  --tests            Run unit + robot tests after build (installs only if tests pass)
  --unit-tests       Run unit tests after build (ctest, installs only if tests pass)
  --robot-tests      Run robot tests after build (installs only if tests pass)
  --no-tests         Skip all tests (default)
  --install-if-tests-pass  Install only if requested tests pass
  --help             Show this help

Notes:
  The install step also installs the stdlib docs (including builtin types)
  to: <prefix>/share/mlang/stdlib
  and stdlib libraries to: <prefix>/lib
  - Install runs by default unless --no-install is set.
  - --tests/--unit-tests/--robot-tests imply --install-if-tests-pass.
  - --install-if-tests-pass requires tests to be selected.
  - --no-tests and --no-install clear --install-if-tests-pass.
USAGE
}

install_after_build=true
prefix="${HOME}/.local"
bin_dir=""
use_sudo=false
build_dir="build"
generator="Ninja"
build_all=true
run_unit_tests=false
run_robot_tests=false
install_if_tests_pass=false

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
      install_if_tests_pass=false
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
    --bin-dir)
      if [[ $# -lt 2 ]]; then
        echo "error: --bin-dir requires a value" >&2
        exit 1
      fi
      bin_dir="$2"
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
    --all)
      build_all=true
      shift
      ;;
    --tests)
      run_unit_tests=true
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --unit-tests)
      run_unit_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --robot-tests)
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --no-tests)
      run_unit_tests=false
      run_robot_tests=false
      install_if_tests_pass=false
      shift
      ;;
    --install-if-tests-pass)
      install_if_tests_pass=true
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$bin_dir" ]]; then
  bin_dir="${prefix}/bin"
fi

cmake -S . -B "$build_dir" -G "$generator" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
if $build_all; then
  cmake --build "$build_dir" --target mlang mlangd mlang_std
fi

# Build mlangd-mla with the freshly built compiler/runtime.
"$build_dir/mlang" "tools/mlangd-mla/main.mla" -L "$build_dir" -lmlang_std -o "$build_dir/mlangd-mla"
# Build mlang-format (Mlang implementation, port-in-progress).
"$build_dir/mlang" "tools/mlang-format-mla/main.mla" -L "$build_dir" -lmlang_std -o "$build_dir/mlang-format"

if $run_unit_tests; then
  ./tests/run_tests.sh --output-on-failure
fi

if $run_robot_tests; then
  ./tests/run_examples_robot.sh
fi

if $install_after_build; then
  if $install_if_tests_pass && ! $run_unit_tests && ! $run_robot_tests; then
    echo "error: --install-if-tests-pass requires --tests, --unit-tests, or --robot-tests" >&2
    exit 1
  fi
  if $use_sudo; then
    sudo cmake --install "$build_dir" --prefix "$prefix"
  else
    cmake --install "$build_dir" --prefix "$prefix"
  fi

  # Install mlangd-mla binary explicitly to the selected bin dir.
  if $use_sudo; then
    sudo mkdir -p "$bin_dir"
    sudo cp -f "$build_dir/mlangd-mla" "$bin_dir/mlangd-mla"
    sudo chmod +x "$bin_dir/mlangd-mla"
    sudo cp -f "$build_dir/mlang-format" "$bin_dir/mlang-format"
    sudo chmod +x "$bin_dir/mlang-format"
  else
    mkdir -p "$bin_dir"
    cp -f "$build_dir/mlangd-mla" "$bin_dir/mlangd-mla"
    chmod +x "$bin_dir/mlangd-mla"
    cp -f "$build_dir/mlang-format" "$bin_dir/mlang-format"
    chmod +x "$bin_dir/mlang-format"
  fi
  echo "installed mlangd-mla: $bin_dir/mlangd-mla"
  echo "installed mlang-format: $bin_dir/mlang-format"

  stdlib_dir="$prefix/share/mlang/stdlib"
  stdlib_lib_dir="$prefix/lib"
  stdlib_lib_src_dir="$prefix/lib/mlang"
  if [[ -f "$stdlib_dir/types.mla" ]]; then
    echo "installed stdlib docs: $stdlib_dir"
  else
    echo "warning: stdlib docs not found at $stdlib_dir" >&2
    echo "         ensure stdlib/ is present in the repo and install again" >&2
  fi
  # Ensure stdlib lib lives in <prefix>/lib
  if [[ -d "$stdlib_lib_src_dir" ]]; then
    if [[ -f "$stdlib_lib_src_dir/libmlang_std.a" ]]; then
      cp -f "$stdlib_lib_src_dir/libmlang_std.a" "$stdlib_lib_dir/"
    fi
    if [[ -f "$stdlib_lib_src_dir/libmlang_std.dylib" ]]; then
      cp -f "$stdlib_lib_src_dir/libmlang_std.dylib" "$stdlib_lib_dir/"
    fi
    if [[ -f "$stdlib_lib_src_dir/libmlang_std.so" ]]; then
      cp -f "$stdlib_lib_src_dir/libmlang_std.so" "$stdlib_lib_dir/"
    fi
  fi

  if [[ -f "$stdlib_lib_dir/libmlang_std.a" || -f "$stdlib_lib_dir/libmlang_std.dylib" || -f "$stdlib_lib_dir/libmlang_std.so" ]]; then
    echo "installed stdlib lib: $stdlib_lib_dir"
  else
    echo "warning: stdlib lib not found at $stdlib_lib_dir" >&2
    echo "         ensure stdlib/src is present and install again" >&2
  fi
else
  echo "note: stdlib docs (builtin type definitions) are not installed" >&2
  echo "      run with --install to copy stdlib to <prefix>/share/mlang/stdlib" >&2
fi
