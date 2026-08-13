#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
EXAMPLE_DIR="$ROOT/examples/qemu_aarch64_bootloader"
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
    echo "Build it first with: cmake --build $ROOT/build" >&2
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

"$MLANG" --target-arch aarch64 -emit-llvm --no-tests -O0 \
    "$EXAMPLE_DIR/kernel.mla" -o "$BUILD_DIR/kernel.ll"

"$CLANG" --target=aarch64-none-elf -ffreestanding -fno-stack-protector \
    -c "$BUILD_DIR/kernel.ll" -o "$BUILD_DIR/kernel.o"
"$CLANG" --target=aarch64-none-elf -ffreestanding \
    -c "$EXAMPLE_DIR/boot.S" -o "$BUILD_DIR/boot.o"
"$CLANG" --target=aarch64-none-elf -ffreestanding \
    -c "$EXAMPLE_DIR/runtime.S" -o "$BUILD_DIR/runtime.o"

"$LD_LLD" -T "$EXAMPLE_DIR/linker.ld" --build-id=none -nostdlib \
    "$BUILD_DIR/boot.o" "$BUILD_DIR/kernel.o" "$BUILD_DIR/runtime.o" \
    -o "$BUILD_DIR/kernel.elf"
"$OBJCOPY" -O binary "$BUILD_DIR/kernel.elf" "$BUILD_DIR/kernel.bin"

echo "Built $BUILD_DIR/kernel.bin"
