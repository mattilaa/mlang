#!/bin/sh
set -eu

EXAMPLE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "qemu-system-i386 is required." >&2
    echo "macOS: brew install qemu" >&2
    echo "Debian/Ubuntu: sudo apt install qemu-system-x86" >&2
    exit 1
fi

"$EXAMPLE_DIR/build.sh"

exec qemu-system-i386 \
    -drive format=raw,file="$EXAMPLE_DIR/build/disk.img",if=floppy \
    -display none \
    -serial none \
    -monitor none \
    -debugcon stdio
