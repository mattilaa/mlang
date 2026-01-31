#!/bin/bash

# MLA Test Runner Script
#
# Usage:
#   ./run_tests.sh                           # Auto-detect compiler
#   ./run_tests.sh /path/to/mlang           # Specify compiler path
#   ./run_tests.sh --output-on-failure      # Pass args to ctest
#   ./run_tests.sh /path/to/mlang -- -V     # Compiler + ctest args

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_PATH=""
CTEST_ARGS=()

if [ $# -gt 0 ]; then
    if [ "$1" = "--" ]; then
        shift
        CTEST_ARGS+=("$@")
    elif [[ "$1" == -* ]]; then
        CTEST_ARGS+=("$@")
    else
        COMPILER_PATH="$1"
        shift
        if [ "${1:-}" = "--" ]; then
            shift
        fi
        if [ $# -gt 0 ]; then
            CTEST_ARGS+=("$@")
        fi
    fi
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
        echo "Usage: $0 [path_to_mlang]"
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

# Create build directory for tests
BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo ""
echo "Configuring tests..."
cmake -DMLA_COMPILER="$COMPILER_PATH" ..

# Build tests
echo ""
echo "Building tests..."
cmake --build . --parallel

# Run tests
echo ""
echo "=========================================="
echo "Running MLA tests..."
echo "=========================================="
if [ ${#CTEST_ARGS[@]} -eq 0 ]; then
    CTEST_ARGS+=(--output-on-failure)
fi
ctest "${CTEST_ARGS[@]}"
