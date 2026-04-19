#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

WAV_PATH="${1:-examples/fft_example/illusion.wav}"
OUT_DIR="/tmp/mlang_oscillator_demo"
MLA_OBJ="${OUT_DIR}/oscillator_demo_mla.o"
BRIDGE_OBJ="${OUT_DIR}/oscillator_demo_bridge.o"
BIN="${OUT_DIR}/oscillator_demo"

mkdir -p "$OUT_DIR"

if [[ ! -f "$WAV_PATH" ]]; then
  echo "Audio file not found: $WAV_PATH" >&2
  exit 1
fi

echo "[oscillator_demo] compiling MLang frontend..."
./build/mlang -c examples/oscillator_demo.mla -o "$MLA_OBJ"

echo "[oscillator_demo] compiling CoreAudio bridge..."
clang++ -std=c++17 -O2 -I./include \
  -c examples/oscillator_demo_bridge.cpp -o "$BRIDGE_OBJ"

echo "[oscillator_demo] linking demo..."
clang++ "$MLA_OBJ" "$BRIDGE_OBJ" build/libmlang_std.a \
  -framework AudioToolbox -framework CoreFoundation \
  -o "$BIN"

echo "[oscillator_demo] running: $WAV_PATH"
"$BIN" "$WAV_PATH"
