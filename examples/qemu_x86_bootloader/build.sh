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
"$MLANG" --target-arch x86 -emit-llvm --no-tests -O0 \
    "$EXAMPLE_DIR/kernel.mla" -o "$BUILD_DIR/kernel.ll"
"$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
    -fno-stack-protector \
    -c "$BUILD_DIR/boot.ll" -o "$BUILD_DIR/boot.o"
"$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
    -fno-stack-protector \
    -c "$BUILD_DIR/kernel.ll" -o "$BUILD_DIR/kernel.o"
"$LD_LLD" -m elf_i386 --image-base=0 --section-start=.boot=0x7c00 \
    --entry=_start --build-id=none -nostdlib "$BUILD_DIR/boot.o" \
    -o "$BUILD_DIR/boot.elf"
"$LD_LLD" -m elf_i386 --image-base=0 --section-start=.kernel=0x10000 \
    --entry=kernel_start --build-id=none -nostdlib "$BUILD_DIR/kernel.o" \
    -o "$BUILD_DIR/kernel.elf"
"$OBJCOPY" -O binary "$BUILD_DIR/boot.elf" "$BUILD_DIR/boot.img"
"$OBJCOPY" -O binary "$BUILD_DIR/kernel.elf" "$BUILD_DIR/kernel.img"

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

kernel_size=$(wc -c < "$BUILD_DIR/kernel.img" | tr -d ' ')
if [ "$kernel_size" -gt 2048 ]; then
    echo "kernel image exceeds the four sectors loaded by boot.mla: $kernel_size bytes" >&2
    exit 1
fi

dd if=/dev/zero of="$BUILD_DIR/disk.img" bs=512 count=2880 >/dev/null 2>&1
dd if="$BUILD_DIR/boot.img" of="$BUILD_DIR/disk.img" conv=notrunc >/dev/null 2>&1
dd if="$BUILD_DIR/kernel.img" of="$BUILD_DIR/disk.img" bs=512 seek=1 \
    conv=notrunc >/dev/null 2>&1

disk_size=$(wc -c < "$BUILD_DIR/disk.img" | tr -d ' ')
if [ "$disk_size" -ne 1474560 ]; then
    echo "disk image must be exactly 1474560 bytes, got $disk_size" >&2
    exit 1
fi

echo "Built $BUILD_DIR/disk.img"
echo "  boot sector: 512 bytes, BIOS signature 55aa"
echo "  loaded kernel: $kernel_size bytes in sectors 2-5"
