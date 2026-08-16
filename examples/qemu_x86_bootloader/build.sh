#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
EXAMPLE_DIR="$ROOT/examples/qemu_x86_bootloader"
BUILD_DIR="$EXAMPLE_DIR/build"
MLANG=${MLANG:-"$ROOT/build/mlang"}

FILESYSTEM_KIB=1024
if [ "${1:-}" = "--filesystem-kib" ]; then
    if [ "$#" -ne 2 ]; then
        echo "usage: ./build.sh [--filesystem-kib KIB]" >&2
        exit 1
    fi
    FILESYSTEM_KIB=$2
elif [ "$#" -ne 0 ]; then
    echo "usage: ./build.sh [--filesystem-kib KIB]" >&2
    exit 1
fi

case "$FILESYSTEM_KIB" in
    ''|*[!0-9]*)
        echo "filesystem size must be a positive integer in KiB" >&2
        exit 1
        ;;
esac
if [ "$FILESYSTEM_KIB" -lt 44 ]; then
    echo "filesystem size must be at least 44 KiB for the native /bin commands" >&2
    exit 1
fi
FILESYSTEM_SIZE=$((FILESYSTEM_KIB * 1024))
if [ "$FILESYSTEM_SIZE" -gt 1073741824 ]; then
    echo "filesystem size must fit in the 32-bit guest address space (maximum 1048576 KiB)" >&2
    exit 1
fi

find_tool() {
    name=$1
    shift
    for candidate in "$@"; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    if command -v "$name" >/dev/null 2>&1; then
        command -v "$name"
        return 0
    fi
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
"$MLANG" --target-arch x86 -emit-llvm --no-tests -Oz \
    "$EXAMPLE_DIR/kernel.mla" -o "$BUILD_DIR/kernel.ll"
"$MLANG" --target-arch x86 -emit-llvm --no-tests -Oz \
    "$EXAMPLE_DIR/filesystem.mla" -o "$BUILD_DIR/filesystem.ll"
"$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
    -fno-stack-protector \
    -c "$BUILD_DIR/boot.ll" -o "$BUILD_DIR/boot.o"
"$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
    -fno-stack-protector \
    -c "$BUILD_DIR/kernel.ll" -o "$BUILD_DIR/kernel.o"
"$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
    -fno-stack-protector \
    -c "$BUILD_DIR/filesystem.ll" -o "$BUILD_DIR/filesystem.o"
"$LD_LLD" -m elf_i386 --image-base=0 --section-start=.boot=0x7c00 \
    --entry=_start --build-id=none -nostdlib "$BUILD_DIR/boot.o" \
    -o "$BUILD_DIR/boot.elf"
"$LD_LLD" -m elf_i386 --image-base=0 --section-start=.kernel=0x10000 \
    --entry=kernel_start --build-id=none -nostdlib "$BUILD_DIR/kernel.o" \
    -o "$BUILD_DIR/kernel.elf"

for command in ls cat chmod chown mkdir rm echo cp mv wc ping vi; do
    command_source="$EXAMPLE_DIR/commands/$command.mla"
    if [ "$command" = "vi" ]; then
        command_source="$EXAMPLE_DIR/vi.mla"
    fi
    "$MLANG" --target-arch x86 -emit-llvm --no-tests -Oz \
        "$command_source" -o "$BUILD_DIR/command-$command.ll"
    "$CLANG" --target=i386-none-elf -Wno-override-module -ffreestanding \
        -fno-stack-protector -c "$BUILD_DIR/command-$command.ll" \
        -o "$BUILD_DIR/command-$command.o"
    "$LD_LLD" -m elf_i386 -T "$EXAMPLE_DIR/command.ld" \
        --entry=command_start --build-id=none -nostdlib \
        --just-symbols="$BUILD_DIR/kernel.elf" \
        "$BUILD_DIR/command-$command.o" -o "$BUILD_DIR/command-$command.elf"
    "$OBJCOPY" -O binary "$BUILD_DIR/command-$command.elf" \
        "$BUILD_DIR/command-$command.img"
done

"$LD_LLD" -m elf_i386 --image-base=0 --section-start=.filesystem=0x20000 \
    --entry=filesystem_start --build-id=none -nostdlib \
    "$BUILD_DIR/filesystem.o" -o "$BUILD_DIR/filesystem.elf"
"$OBJCOPY" -O binary "$BUILD_DIR/boot.elf" "$BUILD_DIR/boot.img"
"$OBJCOPY" -O binary "$BUILD_DIR/kernel.elf" "$BUILD_DIR/kernel.img"
"$OBJCOPY" -O binary "$BUILD_DIR/filesystem.elf" "$BUILD_DIR/filesystem.seed.img"

write_u32() {
    output=$1
    position=$2
    value=$3
    byte0=$((value & 255))
    byte1=$(((value >> 8) & 255))
    byte2=$(((value >> 16) & 255))
    byte3=$(((value >> 24) & 255))
    encoded=$(printf '\\%03o\\%03o\\%03o\\%03o' \
        "$byte0" "$byte1" "$byte2" "$byte3")
    printf '%b' "$encoded" | dd of="$output" bs=1 seek="$position" \
        conv=notrunc >/dev/null 2>&1
}

set -- $(date '+%y %m %d %H %M')
timestamp_year=$((1$1 - 100))
timestamp_month=$((1$2 - 100))
timestamp_day=$((1$3 - 100))
timestamp_hour=$((1$4 - 100))
timestamp_minute=$((1$5 - 100))
seed_timestamp=$(((timestamp_year << 20) | (timestamp_month << 16) | \
    (timestamp_day << 11) | (timestamp_hour << 6) | timestamp_minute))
timestamp_index=0
while [ "$timestamp_index" -lt 21 ]; do
    write_u32 "$BUILD_DIR/filesystem.seed.img" \
        $((16 + 32 * 48 + timestamp_index * 4)) "$seed_timestamp"
    timestamp_index=$((timestamp_index + 1))
done

command_index=2
for command in vi ls cat chmod chown mkdir rm echo cp mv wc ping; do
    command_image="$BUILD_DIR/command-$command.img"
    command_size=$(wc -c < "$command_image" | tr -d ' ')
    if [ "$command_size" -gt 196608 ]; then
        echo "$command command exceeds the 192 KiB runtime command area: $command_size bytes" >&2
        exit 1
    fi
    seed_size=$(wc -c < "$BUILD_DIR/filesystem.seed.img" | tr -d ' ')
    command_offset=$(((seed_size + 15) / 16 * 16))
    if [ "$command_offset" -gt "$seed_size" ]; then
        dd if=/dev/zero of="$BUILD_DIR/filesystem.seed.img" bs=1 count=0 \
            seek="$command_offset" >/dev/null 2>&1
    fi
    dd if="$command_image" of="$BUILD_DIR/filesystem.seed.img" bs=1 \
        seek="$command_offset" conv=notrunc >/dev/null 2>&1
    entry_offset=$((16 + command_index * 48))
    write_u32 "$BUILD_DIR/filesystem.seed.img" $((entry_offset + 36)) "$command_offset"
    write_u32 "$BUILD_DIR/filesystem.seed.img" $((entry_offset + 40)) "$command_size"
    write_u32 "$BUILD_DIR/filesystem.seed.img" $((entry_offset + 44)) "$command_size"
    command_index=$((command_index + 1))
done
filesystem_used=$(wc -c < "$BUILD_DIR/filesystem.seed.img" | tr -d ' ')
write_u32 "$BUILD_DIR/filesystem.seed.img" 8 "$filesystem_used"

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
if [ "$kernel_size" -gt 49152 ]; then
    echo "kernel image exceeds the 96 sectors loaded by boot.mla: $kernel_size bytes" >&2
    exit 1
fi

filesystem_seed_size=$(wc -c < "$BUILD_DIR/filesystem.seed.img" | tr -d ' ')
if [ "$filesystem_seed_size" -gt "$FILESYSTEM_SIZE" ]; then
    echo "filesystem seed requires $filesystem_seed_size bytes, configured size is $FILESYSTEM_SIZE" >&2
    exit 1
fi

filesystem_sectors=$((FILESYSTEM_SIZE / 512))
dd if=/dev/zero of="$BUILD_DIR/filesystem.img" bs=512 count=0 \
    seek="$filesystem_sectors" >/dev/null 2>&1
dd if="$BUILD_DIR/filesystem.seed.img" of="$BUILD_DIR/filesystem.img" \
    conv=notrunc >/dev/null 2>&1

b0=$((FILESYSTEM_SIZE & 255))
b1=$(((FILESYSTEM_SIZE >> 8) & 255))
b2=$(((FILESYSTEM_SIZE >> 16) & 255))
b3=$(((FILESYSTEM_SIZE >> 24) & 255))
size_bytes=$(printf '\\%03o\\%03o\\%03o\\%03o' "$b0" "$b1" "$b2" "$b3")
printf '%b' "$size_bytes" | dd of="$BUILD_DIR/filesystem.img" bs=1 seek=12 \
    conv=notrunc >/dev/null 2>&1

filesystem_size=$(wc -c < "$BUILD_DIR/filesystem.img" | tr -d ' ')
if [ "$filesystem_size" -ne "$FILESYSTEM_SIZE" ]; then
    echo "filesystem image must be $FILESYSTEM_SIZE bytes, got $filesystem_size" >&2
    exit 1
fi

disk_sectors=$((100 + filesystem_sectors))
dd if=/dev/zero of="$BUILD_DIR/disk.img" bs=512 count=0 \
    seek="$disk_sectors" >/dev/null 2>&1
dd if="$BUILD_DIR/boot.img" of="$BUILD_DIR/disk.img" conv=notrunc >/dev/null 2>&1
dd if="$BUILD_DIR/kernel.img" of="$BUILD_DIR/disk.img" bs=512 seek=1 \
    conv=notrunc >/dev/null 2>&1
filesystem_seed_sectors=$(((filesystem_seed_size + 511) / 512))
dd if="$BUILD_DIR/filesystem.img" of="$BUILD_DIR/disk.img" bs=512 seek=100 \
    count="$filesystem_seed_sectors" conv=notrunc >/dev/null 2>&1

disk_size=$(wc -c < "$BUILD_DIR/disk.img" | tr -d ' ')
expected_disk_size=$((disk_sectors * 512))
if [ "$disk_size" -ne "$expected_disk_size" ]; then
    echo "disk image must be exactly $expected_disk_size bytes, got $disk_size" >&2
    exit 1
fi

printf '%s\n' "$FILESYSTEM_KIB" > "$BUILD_DIR/filesystem_kib"

echo "Built $BUILD_DIR/disk.img"
echo "  boot sector: 512 bytes, BIOS signature 55aa"
echo "  loaded kernel: $kernel_size bytes in sectors 2-97"
echo "  native commands: /bin/ls /bin/cat /bin/chmod /bin/chown /bin/mkdir /bin/rm /bin/echo /bin/cp /bin/mv /bin/wc /bin/ping /bin/vi"
echo "  MFS2 filesystem: $filesystem_size bytes ($filesystem_sectors sectors) from LBA 100"
echo "  disk image: $disk_size bytes"
