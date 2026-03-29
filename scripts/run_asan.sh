#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build-asan}"
ARTIFACTS_DIR="${ARTIFACTS_DIR:-artifacts-asan}"

echo "Running ASAN build and tests"
echo "  build dir: $BUILD_DIR"
echo "  artifacts: $ARTIFACTS_DIR"

exec "$REPO_ROOT/scripts/build_install.sh" \
  --asan \
  --build-dir "$BUILD_DIR" \
  --artifacts-dir "$ARTIFACTS_DIR" \
  --unit-tests \
  --robot-tests \
  --no-install \
  "$@"
