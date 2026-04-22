#!/bin/sh
set -eu

BUSYBOX_BIN=$1
APPLETS_H=$2
ROOTFS=$3

install_path_for_dir() {
  case "$1" in
    BB_DIR_BIN) printf '%s\n' "bin" ;;
    BB_DIR_SBIN) printf '%s\n' "sbin" ;;
    BB_DIR_USR_BIN) printf '%s\n' "usr/bin" ;;
    BB_DIR_USR_SBIN) printf '%s\n' "usr/sbin" ;;
    *) return 1 ;;
  esac
}

mkdir -p "$ROOTFS/bin" "$ROOTFS/sbin" "$ROOTFS/usr/bin" "$ROOTFS/usr/sbin"
cp "$BUSYBOX_BIN" "$ROOTFS/bin/busybox"

sed -nE \
  -e 's/.*APPLET[^ (]*\(([A-Za-z0-9_]+),[[:space:]]*[^,]+,[[:space:]]*(BB_DIR_[A-Z_]+).*/\1 \2/p' \
  -e 's/.*APPLET_ODDNAME\([^,]+,[[:space:]]*([^,]+),[[:space:]]*(BB_DIR_[A-Z_]+).*/\1 \2/p' \
  "$APPLETS_H" |
while read -r applet dir_macro; do
  [ -n "$applet" ] || continue
  [ "$applet" = "busybox" ] && continue
  install_rel=$(install_path_for_dir "$dir_macro" || true)
  [ -n "${install_rel:-}" ] || continue
  dest="$ROOTFS/$install_rel/$applet"
  rm -f "$dest"
  ln "$ROOTFS/bin/busybox" "$dest"
done
