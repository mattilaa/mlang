#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MLANG_BIN="${MLANG_BIN:-$ROOT_DIR/build/mlang}"

if [[ ! -x "$MLANG_BIN" ]]; then
  echo "error: mlang binary not found at $MLANG_BIN" >&2
  echo "build it first (e.g., cmake --build build)" >&2
  exit 1
fi

PYTHON_BIN="${PYTHON_BIN:-python3}"
if [[ -x "$ROOT_DIR/.venv/bin/python3" ]]; then
  PYTHON_BIN="$ROOT_DIR/.venv/bin/python3"
elif [[ -x "$ROOT_DIR/.venv/bin/python" ]]; then
  PYTHON_BIN="$ROOT_DIR/.venv/bin/python"
fi

ROBOT_CMD=()

if [[ -n "${VIRTUAL_ENV:-}" && -x "$VIRTUAL_ENV/bin/robot" ]]; then
  ROBOT_CMD=("$VIRTUAL_ENV/bin/robot")
elif [[ -x "$ROOT_DIR/.venv/bin/robot" ]]; then
  ROBOT_CMD=("$ROOT_DIR/.venv/bin/robot")
else
  ROBOT_CMD=("$PYTHON_BIN" "-m" "robot")
fi

if [[ "${ROBOT_DEBUG:-0}" == "1" ]]; then
  echo "debug: ROBOT_CMD=${ROBOT_CMD[*]}" >&2
fi

rc=0
if ! "${ROBOT_CMD[@]}" --version >/dev/null 2>&1; then
  rc=$?
fi
if [[ ${rc} -ne 0 && ${rc} -ne 251 ]]; then
  ROBOT_CMD=()
fi

if [[ ${#ROBOT_CMD[@]} -eq 0 ]]; then
  echo "note: this script depends on Robot Framework." >&2
  echo "error: Robot Framework not found." >&2
  echo "install with: python3 -m pip install -r tests/requirements.txt" >&2
  echo "venv setup:" >&2
  echo "  python3 -m venv .venv" >&2
  echo "  source .venv/bin/activate" >&2
  echo "  python -m pip install -r tests/requirements.txt" >&2
  echo "debug:" >&2
  echo "  VIRTUAL_ENV=${VIRTUAL_ENV:-<unset>}" >&2
  echo "  PYTHON_BIN=$PYTHON_BIN" >&2
  exit 1
fi

"${ROBOT_CMD[@]}" \
  --console dotted \
  --consolecolors off \
  --consolewidth 120 \
  --variable MLANG:"$MLANG_BIN" \
  --outputdir "$ROOT_DIR/build/robot" \
  "$ROOT_DIR/tests/robot/examples.robot"
