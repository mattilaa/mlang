#!/bin/bash

# MLA Test Runner Script
# This script builds the project with tests and runs them
#
# Usage: 
#   ./tests/run_tests.sh          # Run from project root
#   ./run_tests.sh                # Run from tests directory

set -e

# Find project root (where main CMakeLists.txt is)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if we're in the tests directory or project root
if [ -f "$SCRIPT_DIR/CMakeLists.txt" ] && grep -q "add_subdirectory(tests)" "$SCRIPT_DIR/CMakeLists.txt" 2>/dev/null; then
    PROJECT_ROOT="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../CMakeLists.txt" ]; then
    PROJECT_ROOT="$SCRIPT_DIR/.."
else
    echo "Error: Cannot find project root. Run this script from the project root or tests directory."
    exit 1
fi

PROJECT_ROOT=$(realpath "$PROJECT_ROOT")
BUILD_DIR="$PROJECT_ROOT/build"

echo "Project root: $PROJECT_ROOT"
echo "Build directory: $BUILD_DIR"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake (enable tests)
echo ""
echo "Configuring project..."
cmake -DBUILD_TESTS=ON ..

# Build everything (compiler + tests)
echo ""
echo "Building project and tests..."
cmake --build . --parallel

# Run tests
echo ""
echo "=========================================="
echo "Running MLA tests..."
echo "=========================================="
ctest --output-on-failure

# Or run directly with more verbose output:
# ./tests/mla_tests
