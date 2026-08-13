#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
EXAMPLE_DIR="$ROOT/examples/qemu_x86_bootloader"
BUILD_DIR="$EXAMPLE_DIR/build"
MLANG=${MLANG:-"$ROOT/build/mlang"}

find_tool() {
    name=$1
    shift
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return 0
    fi
    for candidate in "$@"; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

if [ ! -x "$MLANG" ]; then
    echo "MLang compiler not found at: $MLANG" >&2
    echo "Build it first with: cmake --build $ROOT/build --target mlang" >&2
    exit 1
fi

CLANG=$(find_tool clang /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang) || {
    echo "clang is required. Install LLVM first." >&2
    exit 1
}
LD_LLD=$(find_tool ld.lld /opt/homebrew/opt/lld/bin/ld.lld /usr/local/opt/lld/bin/ld.lld) || {
    echo "ld.lld is required. Install LLVM lld first." >&2
    exit 1
}
OBJCOPY=$(find_tool llvm-objcopy /opt/homebrew/opt/llvm/bin/llvm-objcopy /usr/local/opt/llvm/bin/llvm-objcopy) || {
    echo "llvm-objcopy is required. Install LLVM first." >&2
    exit 1
}

mkdir -p "$BUILD_DIR"

"$MLANG" --target-arch x86 -emit-llvm --no-tests -O0 \
    "$EXAMPLE_DIR/boot.mla" -o "$BUILD_DIR/boot.ll"
"$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
    -fno-stack-protector \
    -c "$BUILD_DIR/boot.ll" -o "$BUILD_DIR/boot.o"
"$LD_LLD" -m elf_i386 --image-base=0 --section-start=.boot=0x7c00 \
    --entry=_start --build-id=none -nostdlib "$BUILD_DIR/boot.o" \
    -o "$BUILD_DIR/boot.elf"
"$OBJCOPY" -O binary "$BUILD_DIR/boot.elf" "$BUILD_DIR/boot.img"

size=$(wc -c < "$BUILD_DIR/boot.img" | tr -d ' ')
if [ "$size" -ne 512 ]; then
    echo "boot image must be exactly 512 bytes, got $size" >&2
    exit 1
fi

signature=$(od -An -tx1 -j510 -N2 "$BUILD_DIR/boot.img" | tr -d ' \n')
if [ "$signature" != "55aa" ]; then
    echo "boot image has invalid BIOS signature: $signature" >&2
    exit 1
fi

echo "Built $BUILD_DIR/boot.img (512 bytes, BIOS signature 55aa)"
