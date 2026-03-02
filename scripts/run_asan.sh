#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-asan"

echo "Cleaning $BUILD_DIR"
rm -rf "$BUILD_DIR"

ASAN_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1 -fno-optimize-sibling-calls"
ASAN_CXX_FLAGS="$ASAN_C_FLAGS"
ASAN_LINK_FLAGS="-fsanitize=address -fno-omit-frame-pointer"

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -GNinja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="${ASAN_C_FLAGS}" \
    -DCMAKE_CXX_FLAGS="${ASAN_CXX_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${ASAN_LINK_FLAGS}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${ASAN_LINK_FLAGS}" \
    -DCMAKE_MODULE_LINKER_FLAGS="${ASAN_LINK_FLAGS}"

cmake --build "$BUILD_DIR"

ASAN_OPTIONS="${ASAN_OPTIONS:-detect_container_overflow=1:strict_init_order=0}"

"$BUILD_DIR/mlang" --version
ASAN_OPTIONS="$ASAN_OPTIONS" "$BUILD_DIR/mlang" -c tools/mlang-compiler-mla/ast.mla -o "$BUILD_DIR/asan-smoke.o"
rm -f "$BUILD_DIR/asan-smoke.o"

echo "ASan build succeeded and executables ran without crashing."
