#!/bin/bash

# MLA Test Runner Script
#
# Usage: 
#   ./run_tests.sh                      # Auto-detect compiler
#   ./run_tests.sh /path/to/mylang      # Specify compiler path

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER_PATH="${1:-}"

# Try to find compiler if not specified
if [ -z "$COMPILER_PATH" ]; then
    # Check common locations
    if [ -f "$SCRIPT_DIR/../build/mylang" ]; then
        COMPILER_PATH="$SCRIPT_DIR/../build/mylang"
    elif [ -f "$SCRIPT_DIR/../mylang" ]; then
        COMPILER_PATH="$SCRIPT_DIR/../mylang"
    elif command -v mylang &> /dev/null; then
        COMPILER_PATH=$(command -v mylang)
    else
        echo "Error: Could not find mylang compiler."
        echo ""
        echo "Usage: $0 [path_to_mylang]"
        echo ""
        echo "Please either:"
        echo "  1. Build the compiler first: cd .. && mkdir build && cd build && cmake .. && make"
        echo "  2. Specify the compiler path: $0 /path/to/mylang"
        exit 1
    fi
fi

COMPILER_PATH=$(realpath "$COMPILER_PATH")

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
ctest --output-on-failure
