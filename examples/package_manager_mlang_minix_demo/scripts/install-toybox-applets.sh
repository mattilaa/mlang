#!/bin/sh
set -eu

TOYBOX_BIN="$1"
NEWTOYS_H="$2"
DESTDIR="$3"

mkdir -p "$DESTDIR"
cp "$TOYBOX_BIN" "$DESTDIR/toybox"

grep 'TOYFLAG_BIN' "$NEWTOYS_H" \
  | sed -E 's/.*(NEWTOY|OLDTOY)\(([^,]+),.*/\2/' \
  | grep -E '^[A-Za-z0-9_+.-]+$' \
  | grep -Ev '^[-.]' \
  | sort -u \
  | while IFS= read -r applet; do
      [ "$applet" = "toybox" ] && continue
      ln -sf toybox "$DESTDIR/$applet"
    done
