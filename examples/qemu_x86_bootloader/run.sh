#!/bin/sh
set -eu

EXAMPLE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
NO_BUILD=0
FILESYSTEM_KIB=1024

while [ "$#" -gt 0 ]; do
    case "$1" in
        --help|-h)
            echo "Usage: ./run.sh [--no-build] [--filesystem-kib KIB]"
            echo "  --no-build            boot the existing disk.img without rebuilding"
            echo "  --filesystem-kib KIB  MFS2 image size when building (default: 1024)"
            exit 0
            ;;
        --no-build)
            NO_BUILD=1
            shift
            ;;
        --filesystem-kib)
            if [ "$#" -lt 2 ]; then
                echo "--filesystem-kib requires a value" >&2
                exit 1
            fi
            FILESYSTEM_KIB=$2
            shift 2
            ;;
        *)
            echo "unknown option: $1" >&2
            echo "Usage: ./run.sh [--no-build] [--filesystem-kib KIB]" >&2
            exit 1
            ;;
    esac
done

if [ "$NO_BUILD" -eq 1 ]; then
    if [ ! -f "$EXAMPLE_DIR/build/disk.img" ] || [ ! -f "$EXAMPLE_DIR/build/filesystem_kib" ]; then
        echo "disk image not found; run ./build.sh first" >&2
        exit 1
    fi
    FILESYSTEM_KIB=$(sed -n '1p' "$EXAMPLE_DIR/build/filesystem_kib")
else
    "$EXAMPLE_DIR/build.sh" --filesystem-kib "$FILESYSTEM_KIB"
fi

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "qemu-system-i386 is required." >&2
    echo "macOS: brew install qemu" >&2
    echo "Debian/Ubuntu: sudo apt install qemu-system-x86" >&2
    exit 1
fi

memory_mib=$(((FILESYSTEM_KIB * 2 + 1023) / 1024 + 16))
if [ "$memory_mib" -lt 32 ]; then
    memory_mib=32
fi

exec qemu-system-i386 \
    -m "${memory_mib}M" \
    -drive format=raw,file="$EXAMPLE_DIR/build/disk.img",if=ide,index=0,media=disk \
    -display none \
    -rtc base=localtime \
    -serial stdio \
    -monitor none \
    -debugcon file:"$EXAMPLE_DIR/build/debugcon.log"
