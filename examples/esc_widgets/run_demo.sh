#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_BIN="/tmp/mlang_esc_widgets_demo"

"${ROOT_DIR}/build/mlang" "${ROOT_DIR}/examples/esc_widgets/tracker_ui_demo.mla" -o "${OUT_BIN}"
env -u NO_COLOR FORCE_COLOR=1 "${OUT_BIN}"
