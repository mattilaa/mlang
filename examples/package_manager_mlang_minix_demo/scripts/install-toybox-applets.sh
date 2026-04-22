#!/bin/sh
set -eu

TOYBOX_BIN="$1"
NEWTOYS_H="$2"
DESTDIR="$3"
CLASS="${4:-bin}"

mkdir -p "$DESTDIR"
cp "$TOYBOX_BIN" "$DESTDIR/toybox"

case "$CLASS" in
  bin)
    FILTER='TOYFLAG_BIN'
    EXCLUDE='TOYFLAG_USR|TOYFLAG_SBIN'
    ;;
  usrbin)
    FILTER='TOYFLAG_USR.*TOYFLAG_BIN|TOYFLAG_BIN.*TOYFLAG_USR'
    EXCLUDE='TOYFLAG_SBIN'
    ;;
  sbin)
    FILTER='TOYFLAG_SBIN'
    EXCLUDE='TOYFLAG_USR'
    ;;
  usrsbin)
    FILTER='TOYFLAG_USR.*TOYFLAG_SBIN|TOYFLAG_SBIN.*TOYFLAG_USR'
    EXCLUDE=''
    ;;
  *)
    echo "unknown toybox applet class: $CLASS" >&2
    exit 1
    ;;
esac

grep -E "$FILTER" "$NEWTOYS_H" \
  | { [ -n "$EXCLUDE" ] && grep -Ev "$EXCLUDE" || cat; } \
  | sed -E 's/.*(NEWTOY|OLDTOY)\(([^,]+),.*/\2/' \
  | grep -E '^[A-Za-z0-9_+.-]+$' \
  | grep -Ev '^[-.]' \
  | sort -u \
  | while IFS= read -r applet; do
      [ "$applet" = "toybox" ] && continue
      ln -sf toybox "$DESTDIR/$applet"
    done
