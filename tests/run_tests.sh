#!/bin/bash

# MLA Test Runner Script
#
# Usage:
#   ./run_tests.sh                           # Auto-detect compiler
#   ./run_tests.sh /path/to/mlang           # Specify compiler path
#   ./run_tests.sh -j 8                      # Use 8 parallel jobs
#   ./run_tests.sh --output-on-failure      # Pass args to ctest
#   ./run_tests.sh /path/to/mlang -- -V     # Compiler + ctest args

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER_PATH=""
CTEST_ARGS=()
JOBS=""

detect_jobs() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
        return
    fi
    if command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu
        return
    fi
    if command -v getconf >/dev/null 2>&1; then
        getconf _NPROCESSORS_ONLN
        return
    fi
    echo 1
}

print_usage() {
    echo "Usage: $0 [path_to_mlang] [-j|--jobs N] [-- ctest_args...]"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -j|--jobs)
            if [ $# -lt 2 ]; then
                echo "Error: $1 requires a value."
                print_usage
                exit 1
            fi
            JOBS="$2"
            shift 2
            ;;
        --jobs=*)
            JOBS="${1#*=}"
            shift
            ;;
        -j*)
            JOBS="${1#-j}"
            shift
            ;;
        --)
            shift
            CTEST_ARGS+=("$@")
            break
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        -*)
            CTEST_ARGS+=("$1")
            shift
            ;;
        *)
            if [ -z "$COMPILER_PATH" ]; then
                COMPILER_PATH="$1"
            else
                CTEST_ARGS+=("$1")
            fi
            shift
            ;;
    esac
done

if [ -z "$JOBS" ]; then
    JOBS="$(detect_jobs)"
fi

if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [ "$JOBS" -lt 1 ]; then
    echo "Error: invalid jobs value '$JOBS'."
    print_usage
    exit 1
fi

# Try to find compiler if not specified
if [ -z "$COMPILER_PATH" ]; then
    # Check common locations
    if [ -f "$SCRIPT_DIR/../build/mlang" ]; then
        COMPILER_PATH="$SCRIPT_DIR/../build/mlang"
    elif [ -f "$SCRIPT_DIR/../mlang" ]; then
        COMPILER_PATH="$SCRIPT_DIR/../mlang"
    elif command -v mlang &> /dev/null; then
        COMPILER_PATH=$(command -v mlang)
    else
        echo "Error: Could not find mlang compiler."
        echo ""
        print_usage
        echo ""
        echo "Please either:"
        echo "  1. Build the compiler first: cd .. && mkdir build && cd build && cmake .. && make"
        echo "  2. Specify the compiler path: $0 /path/to/mlang"
        exit 1
    fi
fi

if command -v realpath >/dev/null 2>&1; then
    COMPILER_PATH=$(realpath "$COMPILER_PATH")
else
    COMPILER_PATH=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$COMPILER_PATH")
fi

if [ ! -f "$COMPILER_PATH" ]; then
    echo "Error: Compiler not found at $COMPILER_PATH"
    exit 1
fi

echo "Using compiler: $COMPILER_PATH"
echo "Using jobs: $JOBS"

# Create build directory for tests
BUILD_DIR="${TEST_BUILD_DIR:-$SCRIPT_DIR/build}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo ""
echo "Configuring tests..."
cmake -DMLA_COMPILER="$COMPILER_PATH" "$ROOT_DIR"

# Build tests
echo ""
echo "Building tests..."
cmake --build . --parallel "$JOBS"

# Run tests
echo ""
echo "=========================================="
echo "Running MLA tests..."
echo "=========================================="
if [ ${#CTEST_ARGS[@]} -eq 0 ]; then
    CTEST_ARGS+=(--output-on-failure)
fi
ctest -j "$JOBS" "${CTEST_ARGS[@]}"
