#!/usr/bin/env sh
set -eu

build_dir="build"
jobs=""

usage() {
    cat <<EOF
Usage: ./bootstrap.sh [--build-dir DIR] [--jobs N]

Options:
  --build-dir DIR    CMake build directory (default: build)
  --build-dir=DIR    Same as --build-dir DIR
  -j, --jobs N       Parallel build jobs
  --jobs=N           Same as --jobs N
  -h, --help         Show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --build-dir)
            if [ "$#" -lt 2 ]; then
                echo "bootstrap.sh: --build-dir requires a value" >&2
                exit 2
            fi
            build_dir="$2"
            shift 2
            ;;
        --build-dir=*)
            build_dir="${1#--build-dir=}"
            shift
            ;;
        -j|--jobs)
            if [ "$#" -lt 2 ]; then
                echo "bootstrap.sh: --jobs requires a value" >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        --jobs=*)
            jobs="${1#--jobs=}"
            shift
            ;;
        *)
            echo "bootstrap.sh: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

confirm_yes_no() {
    prompt="$1"
    while true; do
        printf "%s " "$prompt"
        if ! IFS= read -r answer; then
            return 1
        fi
        case "$answer" in
            y|Y) return 0 ;;
            n|N) return 1 ;;
        esac
    done
}

use_ninja=""
if command -v ninja >/dev/null 2>&1; then
    use_ninja="1"
fi

if [ -n "$use_ninja" ]; then
    echo "cmake -S . -B $build_dir -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -G Ninja"
    cmake -S . -B "$build_dir" -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -G Ninja
else
    echo "cmake -S . -B $build_dir -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release"
    cmake -S . -B "$build_dir" -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
fi

build_args="--target mlang mlang_std mlang-config"
if [ -n "$jobs" ]; then
    echo "cmake --build $build_dir --config Release $build_args --parallel $jobs"
    cmake --build "$build_dir" --config Release --target mlang mlang_std mlang-config --parallel "$jobs"
else
    echo "cmake --build $build_dir --config Release $build_args"
    cmake --build "$build_dir" --config Release --target mlang mlang_std mlang-config
fi

mlang_config="$build_dir/mlang-config"
if [ ! -x "$mlang_config" ]; then
    if [ -x "$build_dir/mlang-config.exe" ]; then
        mlang_config="$build_dir/mlang-config.exe"
    elif [ -x "$build_dir/Release/mlang-config.exe" ]; then
        mlang_config="$build_dir/Release/mlang-config.exe"
    else
        echo "bootstrap.sh: cannot find mlang-config in $build_dir after build" >&2
        exit 1
    fi
fi

if confirm_yes_no "Do you want to run mlang-config? (y/n)"; then
    echo "$mlang_config"
    "$mlang_config"

    if confirm_yes_no "Do you want to build the full MLang toolchain now? (y/n)"; then
        echo "./build.sh --build-dir $build_dir"
        ./build.sh --build-dir "$build_dir"
    fi
fi
