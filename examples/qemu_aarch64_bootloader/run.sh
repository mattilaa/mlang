#!/bin/sh
set -eu

EXAMPLE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
QEMU=${QEMU:-qemu-system-aarch64}

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "qemu-system-aarch64 is required to run this example." >&2
    echo "Install QEMU on macOS with: brew install qemu" >&2
    echo "Install QEMU on Debian/Ubuntu with: sudo apt install qemu-system-arm" >&2
    exit 1
fi

"$EXAMPLE_DIR/build.sh"

echo "Starting QEMU. Press Ctrl-a, then x, to exit."
exec "$QEMU" \
    -machine virt \
    -cpu cortex-a57 \
    -m 128M \
    -nographic \
    -monitor none \
    -serial stdio \
    -kernel "$EXAMPLE_DIR/build/kernel.bin"
