#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
RUNTIME_DIR="${RUNTIME_DIR:-/tmp/mlang_macos_stdio_driver}"

IN_FIFO="$RUNTIME_DIR/driver.in"
PID_FILE="$RUNTIME_DIR/driver.pid"
KEEPER_PID_FILE="$RUNTIME_DIR/fifo_keeper.pid"

if [ -p "$IN_FIFO" ]; then
  printf 'detach\n' > "$IN_FIFO" || true
fi

if [ -f "$PID_FILE" ]; then
  PID="$(cat "$PID_FILE")"
  sleep 1
  if kill -0 "$PID" 2>/dev/null; then
    kill "$PID" 2>/dev/null || true
  fi
  rm -f "$PID_FILE"
fi

if [ -f "$KEEPER_PID_FILE" ]; then
  KEEPER_PID="$(cat "$KEEPER_PID_FILE")"
  kill "$KEEPER_PID" 2>/dev/null || true
  rm -f "$KEEPER_PID_FILE"
fi

rm -f "$IN_FIFO"
echo "detached macos stdio driver"
