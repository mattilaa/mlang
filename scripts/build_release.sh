#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build_release.sh --version vMAJOR.MINOR.PATCH [options]

Builds MLang and creates a relocatable release archive containing the compiler,
static runtime library, standard-library modules, standalone modules, README,
license, dependency report, and setup instructions.

Options:
  --version VERSION       Public vMAJOR.MINOR.PATCH release version (required)
  -B, --build-dir DIR     Isolated CMake directory (default: build-release)
  -o, --output-dir DIR    Archive destination (default: dist)
  -j, --jobs N            Parallel build jobs (default: detected CPU count)
  --archive-format FORMAT zip or tar.gz (default: tar.gz)
  --deployment-target V   Override the host macOS deployment target
  --force                 Replace an existing archive and checksum
  -h, --help              Show this help

The unmodified CMake install tree remains in BUILD_DIR/install-root. The macOS
package builder consumes that tree after this script creates the archive.
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
build_dir="build-release"
output_dir="dist"
jobs=""
public_version=""
archive_format="tar.gz"
deployment_target=""
force=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) [[ $# -ge 2 ]] || { echo "error: --version requires a value" >&2; exit 2; }; public_version="$2"; shift 2 ;;
    --version=*) public_version="${1#*=}"; shift ;;
    -B|--build-dir) [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }; build_dir="$2"; shift 2 ;;
    --build-dir=*) build_dir="${1#*=}"; shift ;;
    -o|--output-dir) [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }; output_dir="$2"; shift 2 ;;
    --output-dir=*) output_dir="${1#*=}"; shift ;;
    -j|--jobs) [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }; jobs="$2"; shift 2 ;;
    --jobs=*) jobs="${1#*=}"; shift ;;
    --archive-format) [[ $# -ge 2 ]] || { echo "error: --archive-format requires a value" >&2; exit 2; }; archive_format="$2"; shift 2 ;;
    --archive-format=*) archive_format="${1#*=}"; shift ;;
    --deployment-target) [[ $# -ge 2 ]] || { echo "error: --deployment-target requires a value" >&2; exit 2; }; deployment_target="$2"; shift 2 ;;
    --deployment-target=*) deployment_target="${1#*=}"; shift ;;
    --force) force=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "$public_version" =~ ^v([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || {
  echo "error: --version must use vMAJOR.MINOR.PATCH" >&2
  exit 2
}
release_version="${public_version#v}"
case "$archive_format" in zip|tar.gz) ;; *) echo "error: unsupported archive format '$archive_format'" >&2; exit 2 ;; esac

if [[ -z "$jobs" ]]; then
  jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
  jobs="${jobs:-1}"
fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "error: jobs must be a positive integer" >&2; exit 2; }

required_tools=(cmake file)
[[ "$archive_format" == zip ]] && required_tools+=(zip) || required_tools+=(tar)
for tool in "${required_tools[@]}"; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: required tool '$tool' was not found" >&2; exit 1; }
done

host_os="$(uname -s)"
host_arch="$(uname -m)"
case "$host_arch" in aarch64) host_arch="arm64" ;; amd64) host_arch="x86_64" ;; esac
case "$host_os" in Darwin) platform="macos" ;; Linux) platform="linux" ;; *) echo "error: only macOS and Linux are supported" >&2; exit 1 ;; esac

cd "$repo_root"
mkdir -p "$build_dir" "$output_dir"
echo "==> Configuring MLang $public_version"
configure_args=(
  -S "$repo_root"
  -B "$build_dir"
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX=/usr/local
  -DBUILD_TESTS=OFF
  -DMLANG_RELEASE_VERSION="$release_version"
  -DMLANG_PORTABLE_OPENSSL_LINK_ARGS=ON
  -DMLANGD_BUILD_FORMATTER_DEPENDENCY=OFF
)
if [[ "$platform" == macos && -n "$deployment_target" ]]; then
  configure_args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment_target")
fi
if command -v ninja >/dev/null 2>&1; then
  configure_args+=(-G Ninja)
fi
cmake "${configure_args[@]}"

echo "==> Building compiler and runtime"
cmake --build "$build_dir" --config Release \
  --target mlang mlang_std --parallel "$jobs"

binary="$build_dir/mlang"
[[ -x "$binary" ]] || { echo "error: expected compiler is missing: $binary" >&2; exit 1; }
[[ "$($binary --version | awk 'NR == 1 { print $2 }')" == "${public_version#v}" ]] || {
  echo "error: built compiler version does not match $public_version" >&2
  exit 1
}

install_root="$build_dir/install-root"
cmake -E rm -rf "$install_root"
DESTDIR="$(cd "$build_dir" && pwd)/install-root" \
  cmake --install "$build_dir" --config Release --component mlang-core

install_prefix="$install_root/usr/local"
[[ -x "$install_prefix/bin/mlang" ]] || { echo "error: staged compiler is missing" >&2; exit 1; }
[[ -f "$install_prefix/lib/mlang/libmlang_std.a" ]] || { echo "error: staged runtime library is missing" >&2; exit 1; }

if [[ "$platform" == macos ]]; then
  dependency_report="$(otool -L "$binary")"
else
  dependency_report="$(ldd "$binary" 2>&1 || file "$binary")"
fi

bundle_name="mlang-${public_version}-${platform}-${host_arch}"
archive="$output_dir/$bundle_name.$archive_format"
checksum="$archive.sha256"
if [[ -e "$archive" || -e "$checksum" ]]; then
  $force || { echo "error: output exists; pass --force to replace it: $archive" >&2; exit 1; }
  rm -f -- "$archive" "$checksum"
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/mlang-release.XXXXXX")"
cleanup() { rm -rf -- "$stage_dir"; }
trap cleanup EXIT
bundle_dir="$stage_dir/$bundle_name"
mkdir -p "$bundle_dir"
cp -R "$install_prefix/." "$bundle_dir/"
mkdir -p "$bundle_dir/libexec/mlang"
mv "$bundle_dir/bin/mlang" "$bundle_dir/libexec/mlang/mlang"
cat > "$bundle_dir/bin/mlang" <<'EOF'
#!/usr/bin/env sh
set -eu
root_dir="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
export MLANG_STDLIB_PATH="$root_dir/share/mlang/stdlib${MLANG_STDLIB_PATH:+:$MLANG_STDLIB_PATH}"
export MLANG_MODULE_PATH="$root_dir/share/mlang/modules${MLANG_MODULE_PATH:+:$MLANG_MODULE_PATH}"
export MLANG_STDLIB_LIB_PATH="$root_dir/lib/mlang${MLANG_STDLIB_LIB_PATH:+:$MLANG_STDLIB_LIB_PATH}"
exec "$root_dir/libexec/mlang/mlang" "$@"
EOF
chmod 0755 "$bundle_dir/bin/mlang" "$bundle_dir/libexec/mlang/mlang"
cp "$repo_root/README.md" "$repo_root/LICENSE" "$bundle_dir/"
printf '%s\n' "$dependency_report" > "$bundle_dir/DEPENDENCIES.txt"
if [[ "$platform" == macos ]]; then
  prerequisite_text="Install the runtime toolchain with: brew install llvm openssl@3"
else
  prerequisite_text="Install a C++/Clang toolchain, OpenSSL, z3, and zstd runtime/development packages."
fi
cat > "$bundle_dir/INSTALL.txt" <<EOF
MLang $public_version release archive
Platform: $platform
Architecture: $host_arch

Run in place:
  ./bin/mlang --version

Or add this archive's bin directory to PATH. The wrapper configures the bundled
standard-library, module, and runtime-library paths automatically.

Native programs produced by MLang require a C++ linker and OpenSSL development
libraries. See README.md for the platform dependency commands.

$prerequisite_text
See DEPENDENCIES.txt for the exact native libraries used by this build.
EOF

echo "==> Creating $archive"
if [[ "$archive_format" == zip ]]; then
  archive_abs="$(cd "$output_dir" && pwd)/$(basename "$archive")"
  (cd "$stage_dir" && zip -qr "$archive_abs" "$bundle_name")
else
  COPYFILE_DISABLE=1 tar -czf "$archive" -C "$stage_dir" "$bundle_name"
fi
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$output_dir" && sha256sum "$(basename "$archive")") > "$checksum"
else
  (cd "$output_dir" && shasum -a 256 "$(basename "$archive")") > "$checksum"
fi

echo "Release archive: $archive"
echo "SHA-256:        $checksum"
echo "Install root:  $install_root"
