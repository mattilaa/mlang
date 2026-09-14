#!/bin/bash
# Exercise uninstall without installing anything or requiring administrator access.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/mlang-pkg-test.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
payload="$scratch/payload"
mkdir "$payload"
touch "$payload/hello" "$payload/README.md" "$payload/user-data"
sed -e 's/@NAME@/hello/g' -e 's/@IDENTIFIER@/org.mlang.test/g' \
    -e "s|^prefix=.*|prefix='$payload'|" \
    "$repo/scripts/uninstall_macos_project.sh.in" > "$payload/uninstall.sh"
bash "$payload/uninstall.sh" --dry-run
[[ -f $payload/hello && -f $payload/README.md && -f $payload/uninstall.sh ]]
# Only fake privilege and receipt operations; file removal uses the real rm
# against this test's private directory.
id() { echo 0; }
pkgutil() { echo "$*" >> "$receipt_log"; }
export -f id pkgutil
export receipt_log="$scratch/receipts"
bash "$payload/uninstall.sh" --remove
[[ ! -e $payload/hello && ! -e $payload/README.md && ! -e $payload/uninstall.sh ]]
[[ -f $payload/user-data ]]
grep -q -- '--forget org.mlang.test' "$receipt_log"
mkdir "$scratch/target"
ln -s "$scratch/target" "$scratch/link"
sed -e 's/@NAME@/hello/g' -e 's/@IDENTIFIER@/org.mlang.test/g' \
    -e "s|^prefix=.*|prefix='$scratch/link'|" \
    "$repo/scripts/uninstall_macos_project.sh.in" > "$scratch/refuse.sh"
if bash "$scratch/refuse.sh" --remove; then
    echo 'Expected symlink rejection' >&2
    exit 1
fi
echo 'PASS: dry-run, payload removal, extra-file preservation, receipt cleanup, symlink rejection'
