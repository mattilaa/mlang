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
            echo "  MLANG_QEMU_MAC         override the host-derived guest MAC address"
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

detect_host_mac() {
    if command -v ip >/dev/null 2>&1; then
        ip -o link 2>/dev/null | awk '
            $2 !~ /^lo:/ {
                for (field = 1; field <= NF; field++) {
                    if ($field == "link/ether") {
                        print $(field + 1)
                        exit
                    }
                }
            }
        '
        return
    fi
    if command -v ifconfig >/dev/null 2>&1; then
        ifconfig 2>/dev/null | awk '$1 == "ether" { print $2; exit }'
    fi
}

network_mac=${MLANG_QEMU_MAC:-}
network_mac_source="override"
if [ -z "$network_mac" ]; then
    host_mac=$(detect_host_mac)
    if [ -z "$host_mac" ]; then
        echo "cannot detect a host MAC address; set MLANG_QEMU_MAC" >&2
        exit 1
    fi
    old_ifs=$IFS
    IFS=:
    set -- $host_mac
    IFS=$old_ifs
    if [ "$#" -ne 6 ]; then
        echo "invalid detected host MAC address: $host_mac" >&2
        exit 1
    fi
    first_octet=$(((0x$1 | 2) & 254))
    network_mac=$(printf '%02x:%s:%s:%s:%s:%s' \
        "$first_octet" "$2" "$3" "$4" "$5" "$6")
    network_mac_source="derived from host $host_mac"
fi

memory_mib=$(((FILESYSTEM_KIB * 2 + 1023) / 1024 + 16))
if [ "$memory_mib" -lt 32 ]; then
    memory_mib=32
fi

echo "QEMU network MAC: $network_mac ($network_mac_source)"

exec qemu-system-i386 \
    -m "${memory_mib}M" \
    -drive format=raw,file="$EXAMPLE_DIR/build/disk.img",if=ide,index=0,media=disk \
    -display none \
    -rtc base=localtime \
    -netdev user,id=net0,ipv6=off \
    -device ne2k_isa,netdev=net0,iobase=0x300,irq=9,mac="$network_mac" \
    -serial stdio \
    -monitor none \
    -debugcon file:"$EXAMPLE_DIR/build/debugcon.log"
