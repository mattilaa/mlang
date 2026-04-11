#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
EXAMPLE_DIR="$ROOT_DIR/examples/coreaudio_mverb_demo"
BUILD_DIR="$ROOT_DIR/build/coreaudio_mverb_demo"
BRIDGE_OBJ="$BUILD_DIR/coreaudio_mverb_bridge.o"
MAIN_OBJ="$BUILD_DIR/main.o"
OUT_EXE="$BUILD_DIR/coreaudio_mverb_demo"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This example is macOS-only because it uses CoreAudio/AudioUnit." >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"

cc -std=c11 -O2 -I"$ROOT_DIR/include" \
  -c "$EXAMPLE_DIR/coreaudio_mverb_bridge.c" -o "$BRIDGE_OBJ"

"$ROOT_DIR/build/mlang" -c "$EXAMPLE_DIR/main.mla" -o "$MAIN_OBJ"

cc -O2 "$MAIN_OBJ" "$BRIDGE_OBJ" \
  -L"$ROOT_DIR/build" -lmlang_std -lm \
  -framework AudioToolbox \
  -framework AudioUnit \
  -framework CoreAudio \
  -framework CoreFoundation \
  -o "$OUT_EXE"

exec "$OUT_EXE" "$@"
