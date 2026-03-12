#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_BIN="/tmp/mlang_esc_widgets_demo"

cleanup_terminal() {
  stty sane 2>/dev/null || true
  printf '\033[0m\033[?25h\033[?1049l' || true
}

trap cleanup_terminal EXIT INT TERM

"${ROOT_DIR}/build/mlang-frontend-mla" "${ROOT_DIR}/examples/esc_widgets/tracker_ui_demo.mla" -o "${OUT_BIN}"
env -u NO_COLOR FORCE_COLOR=1 MLANG_FORCE_ANSI=1 "${OUT_BIN}"
