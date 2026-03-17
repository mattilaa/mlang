#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

OUT_BIN="/tmp/mlang_env_help_demo"
./build/mlang examples/env_help_demo/main.mla -L ./build -lmlang_std -o "$OUT_BIN"
"$OUT_BIN" "$@"
