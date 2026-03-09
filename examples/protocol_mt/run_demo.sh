#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_BIN="${ROOT_DIR}/build/mlang"
STDLIB_LIB="${ROOT_DIR}/build/libmlang_std.a"
OUT_DIR="${TMPDIR:-/tmp}/mlang_protocol_mt_demo"

PORT="${PORT:-19095}"
CLIENTS="${CLIENTS:-1}"
ROUNDS="${ROUNDS:-7}"
DELAY_MIN_MS="${DELAY_MIN_MS:-500}"
DELAY_MAX_MS="${DELAY_MAX_MS:-1000}"

mkdir -p "${OUT_DIR}"

echo "[1/3] building protocol_mt server/client examples"
"${BUILD_BIN}" "${ROOT_DIR}/examples/protocol_mt/server.mla" \
  -o "${OUT_DIR}/protocol_mt_server" \
  -L "${ROOT_DIR}/build" -lmlang_std \
  -Wl,-force_load,"${STDLIB_LIB}"

"${BUILD_BIN}" "${ROOT_DIR}/examples/protocol_mt/client.mla" \
  -o "${OUT_DIR}/protocol_mt_client" \
  -L "${ROOT_DIR}/build" -lmlang_std \
  -Wl,-force_load,"${STDLIB_LIB}"

echo "[2/3] starting server on 127.0.0.1:${PORT}"
"${OUT_DIR}/protocol_mt_server" --port "${PORT}" --clients "${CLIENTS}" --rounds "${ROUNDS}" &
SERVER_PID=$!

cleanup() {
  if kill -0 "${SERVER_PID}" >/dev/null 2>&1; then
    kill "${SERVER_PID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

# Give server a moment to bind before client load starts.
sleep 0.2

echo "[3/3] running client load (clients=${CLIENTS}, rounds=${ROUNDS}, delay=${DELAY_MIN_MS}-${DELAY_MAX_MS}ms)"
"${OUT_DIR}/protocol_mt_client" \
  --port "${PORT}" \
  --clients "${CLIENTS}" \
  --rounds "${ROUNDS}" \
  --delay-min-ms "${DELAY_MIN_MS}" \
  --delay-max-ms "${DELAY_MAX_MS}"

wait "${SERVER_PID}"
trap - EXIT

echo "done: protocol_mt demo completed successfully"
echo "binaries: ${OUT_DIR}/protocol_mt_server, ${OUT_DIR}/protocol_mt_client"
