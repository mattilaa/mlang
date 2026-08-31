#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build_macos_pkg.sh --root DIR --version vMAJOR.MINOR.PATCH [options]

Creates an unsigned macOS installer from build_release.sh's install-root.

Options:
  --root DIR           DESTDIR tree containing usr/local/bin/mlang
  --version VERSION    Public vMAJOR.MINOR.PATCH release version
  -o, --output-dir DIR Package destination (default: dist)
  --identifier ID      Package identifier (default: io.github.mattilaa.mlang)
  --sign IDENTITY      Optional Developer ID Installer identity
  --force              Replace an existing package and checksum
  -h, --help           Show this help
EOF
}

install_root=""
public_version=""
output_dir="dist"
identifier="io.github.mattilaa.mlang"
sign_identity=""
force=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --root) [[ $# -ge 2 ]] || { echo "error: --root requires a value" >&2; exit 2; }; install_root="$2"; shift 2 ;;
    --root=*) install_root="${1#*=}"; shift ;;
    --version) [[ $# -ge 2 ]] || { echo "error: --version requires a value" >&2; exit 2; }; public_version="$2"; shift 2 ;;
    --version=*) public_version="${1#*=}"; shift ;;
    -o|--output-dir) [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }; output_dir="$2"; shift 2 ;;
    --output-dir=*) output_dir="${1#*=}"; shift ;;
    --identifier) [[ $# -ge 2 ]] || { echo "error: --identifier requires a value" >&2; exit 2; }; identifier="$2"; shift 2 ;;
    --identifier=*) identifier="${1#*=}"; shift ;;
    --sign) [[ $# -ge 2 ]] || { echo "error: --sign requires a value" >&2; exit 2; }; sign_identity="$2"; shift 2 ;;
    --sign=*) sign_identity="${1#*=}"; shift ;;
    --force) force=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "$(uname -s)" == Darwin ]] || { echo "error: macOS packages must be built on macOS" >&2; exit 1; }
command -v pkgbuild >/dev/null 2>&1 || { echo "error: pkgbuild was not found" >&2; exit 1; }
[[ "$public_version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "error: invalid release version" >&2; exit 2; }
[[ "$identifier" =~ ^[A-Za-z0-9][A-Za-z0-9.-]+$ ]] || { echo "error: invalid package identifier" >&2; exit 2; }
[[ -x "$install_root/usr/local/bin/mlang" ]] || { echo "error: staged compiler is missing under $install_root" >&2; exit 1; }

arch="$(file "$install_root/usr/local/bin/mlang")"
case "$arch" in *arm64*) package_arch="arm64" ;; *x86_64*) package_arch="x86_64" ;; *) echo "error: unsupported compiler architecture: $arch" >&2; exit 1 ;; esac
mkdir -p "$output_dir"
package="$output_dir/mlang-${public_version}-macos-${package_arch}.pkg"
checksum="$package.sha256"
if [[ -e "$package" || -e "$checksum" ]]; then
  $force || { echo "error: output exists; pass --force to replace it: $package" >&2; exit 1; }
  rm -f -- "$package" "$checksum"
fi

xattr -cr "$install_root"
pkg_args=(--root "$install_root" --identifier "$identifier" --version "${public_version#v}" --install-location / --ownership recommended)
[[ -n "$sign_identity" ]] && pkg_args+=(--sign "$sign_identity")
COPYFILE_DISABLE=1 pkgbuild "${pkg_args[@]}" "$package"
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$output_dir" && sha256sum "$(basename "$package")") > "$checksum"
else
  (cd "$output_dir" && shasum -a 256 "$(basename "$package")") > "$checksum"
fi
echo "macOS installer: $package"
echo "SHA-256:         $checksum"
[[ -n "$sign_identity" ]] || echo "warning: package is unsigned" >&2
