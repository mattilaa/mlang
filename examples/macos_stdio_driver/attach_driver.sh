#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
MLANG_BIN="${MLANG_BIN:-$BUILD_DIR/mlang}"
RUNTIME_DIR="${RUNTIME_DIR:-/tmp/mlang_macos_stdio_driver}"

SRC="$SCRIPT_DIR/main.mla"
BIN="$RUNTIME_DIR/macos_stdio_driver_demo"
IN_FIFO="$RUNTIME_DIR/driver.in"
LOG_FILE="$RUNTIME_DIR/driver.log"
PID_FILE="$RUNTIME_DIR/driver.pid"
KEEPER_PID_FILE="$RUNTIME_DIR/fifo_keeper.pid"

mkdir -p "$RUNTIME_DIR"

if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
  echo "driver already attached (pid $(cat "$PID_FILE"))"
  exit 0
fi

rm -f "$IN_FIFO" "$PID_FILE" "$KEEPER_PID_FILE"
mkfifo "$IN_FIFO"

"$MLANG_BIN" "$SRC" -L "$BUILD_DIR" -lmlang_std -o "$BIN"
: > "$LOG_FILE"

(while :; do sleep 3600; done) > "$IN_FIFO" &
KEEPER_PID=$!
echo "$KEEPER_PID" > "$KEEPER_PID_FILE"

"$BIN" < "$IN_FIFO" >> "$LOG_FILE" 2>&1 &
DRIVER_PID=$!
echo "$DRIVER_PID" > "$PID_FILE"

echo "attached macos stdio driver"
echo "  pid: $DRIVER_PID"
echo "  fifo: $IN_FIFO"
echo "  log: $LOG_FILE"
echo "use ./send_command.sh status"
