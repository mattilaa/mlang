#!/bin/bash
# Package one executable and its README, with a standalone uninstaller.
set -euo pipefail
if [[ $# != 6 ]]; then
    echo "Usage: bash scripts/package_macos_project.sh BINARY README NAME IDENTIFIER VERSION OUTPUT.pkg" >&2
    exit 2
fi
binary=$1
readme=$2
name=$3
identifier=$4
version=$5
output=$6
[[ $(uname -s) == Darwin ]] || { echo 'macOS is required' >&2; exit 1; }
[[ $name =~ ^[a-zA-Z0-9][a-zA-Z0-9_-]*$ ]] || { echo 'Invalid name' >&2; exit 2; }
[[ $identifier =~ ^[a-zA-Z0-9]+(\.[a-zA-Z0-9_-]+)+$ ]] || { echo 'Invalid identifier' >&2; exit 2; }
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Expected MAJOR.MINOR.PATCH version' >&2; exit 2; }
[[ -f $binary && -x $binary && -f $readme ]] || { echo 'Executable or README missing' >&2; exit 2; }
[[ $output == *.pkg && ! -e $output && ! -L $output ]] || { echo 'Output must be a new .pkg file' >&2; exit 2; }
script_dir=$(cd "$(dirname "$0")" && pwd)
stage=$(mktemp -d "${TMPDIR:-/tmp}/mlang-project-pkg.XXXXXX")
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$stage/root" "$(dirname "$output")"
install -m 755 "$binary" "$stage/root/$name"
install -m 644 "$readme" "$stage/root/README.md"
sed -e "s/@NAME@/$name/g" -e "s/@IDENTIFIER@/$identifier/g" \
    "$script_dir/uninstall_macos_project.sh.in" > "$stage/root/uninstall.sh"
chmod 755 "$stage/root/uninstall.sh"
# Strip copied filesystem metadata from the private staging tree only.
xattr -cr "$stage/root"
COPYFILE_DISABLE=1 pkgbuild --root "$stage/root" \
    --identifier "$identifier" --version "$version" \
    --install-location "/usr/local/lib/$identifier" \
    --ownership recommended "$output"
echo "Unsigned installer: $output"
echo "Run after installation: /usr/local/lib/$identifier/$name"
echo "Preview uninstall: /bin/bash /usr/local/lib/$identifier/uninstall.sh"
echo "Uninstall: sudo /bin/bash /usr/local/lib/$identifier/uninstall.sh --remove"
