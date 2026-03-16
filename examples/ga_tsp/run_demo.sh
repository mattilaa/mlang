#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="/tmp/mlang_ga_tsp_demo"

cd "${ROOT_DIR}"

./build/mlang examples/ga_tsp/main.mla -o "${BIN}"
"${BIN}"
