#!/bin/sh
set -eu

RUNTIME_DIR="${RUNTIME_DIR:-/tmp/mlang_macos_stdio_driver}"
IN_FIFO="$RUNTIME_DIR/driver.in"
LOG_FILE="$RUNTIME_DIR/driver.log"

if [ ! -p "$IN_FIFO" ]; then
  echo "driver not attached"
  exit 1
fi

if [ "$#" -eq 0 ]; then
  echo "usage: $0 <command>"
  exit 1
fi

printf '%s\n' "$*" > "$IN_FIFO"
sleep 1
if [ -f "$LOG_FILE" ]; then
  tail -n 8 "$LOG_FILE"
fi
