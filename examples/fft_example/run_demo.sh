#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

WAV_PATH="examples/fft_example/illusion.wav"
OUT_EXE="/tmp/mlang_fft_analyzer_demo"
OUT_OBJ="/tmp/fftviz_bridge.o"
OUT_LIB="/tmp/libfftviz_bridge.a"

EXTRA_ARGS=()
WAV_SET=0
for arg in "$@"; do
  case "$arg" in
    --flat|--music)
      EXTRA_ARGS+=("$arg")
      ;;
    *)
      if [[ $WAV_SET -eq 0 ]]; then
        WAV_PATH="$arg"
        WAV_SET=1
      else
        EXTRA_ARGS+=("$arg")
      fi
      ;;
  esac
done

if [[ ! -f "$WAV_PATH" ]]; then
  echo "WAV file not found: $WAV_PATH" >&2
  exit 1
fi

echo "[fft_demo] building JACK bridge..."
cc -O2 -I./include $(pkg-config --cflags jack) \
  -c examples/fft_example/fftviz_bridge.c -o "$OUT_OBJ"
ar rcs "$OUT_LIB" "$OUT_OBJ"

echo "[fft_demo] building MLang demo..."
./build/mlang examples/fft_example/main.mla \
  -L /tmp -lfftviz_bridge $(pkg-config --libs jack) \
  -o "$OUT_EXE"

echo "[fft_demo] running analyzer on: $WAV_PATH args: ${EXTRA_ARGS[*]:-<none>}"
"$OUT_EXE" "$WAV_PATH" "${EXTRA_ARGS[@]}"
