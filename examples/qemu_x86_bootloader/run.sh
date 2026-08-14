#!/bin/sh
set -eu

EXAMPLE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

case "${1:-}" in
    --help|-h)
        echo "Usage: ./run.sh [--no-build]"
        echo "  --no-build  boot the existing disk.img without resetting persisted MFS2 changes"
        exit 0
        ;;
    --no-build)
        if [ ! -f "$EXAMPLE_DIR/build/disk.img" ]; then
            echo "disk image not found; run ./build.sh first" >&2
            exit 1
        fi
        ;;
    "")
        "$EXAMPLE_DIR/build.sh"
        ;;
    *)
        echo "unknown option: $1" >&2
        echo "Usage: ./run.sh [--no-build]" >&2
        exit 1
        ;;
esac

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "qemu-system-i386 is required." >&2
    echo "macOS: brew install qemu" >&2
    echo "Debian/Ubuntu: sudo apt install qemu-system-x86" >&2
    exit 1
fi

exec qemu-system-i386 \
    -drive format=raw,file="$EXAMPLE_DIR/build/disk.img",if=floppy \
    -display none \
    -serial stdio \
    -monitor none \
    -debugcon file:"$EXAMPLE_DIR/build/debugcon.log"
