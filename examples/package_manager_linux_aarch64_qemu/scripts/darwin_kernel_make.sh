#!/bin/sh
set -eu

if [ "$#" -lt 3 ]; then
  echo "usage: $0 <root> <build_dir> <deps_dir> [make args...]"
  exit 1
fi

root="$1"
build_dir="$2"
deps_dir="$3"
shift 3

libelf_prefix="$(brew --prefix libelf 2>/dev/null || true)"
if [ -z "$libelf_prefix" ]; then
  echo "Missing Homebrew libelf. Install with: brew install libelf"
  exit 1
fi

mkdir -p "$build_dir/host-compat"
printf '#include <libelf/sys_elf.h>\n' > "$build_dir/host-compat/elf.h"

export PKG_CONFIG_PATH="$libelf_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export HOSTCFLAGS="-I$build_dir/host-compat -I$libelf_prefix/include -I$libelf_prefix/include/libelf${HOSTCFLAGS:+ $HOSTCFLAGS}"
export HOST_EXTRACFLAGS="$HOSTCFLAGS"
export HOSTLDFLAGS="-L$libelf_prefix/lib${HOSTLDFLAGS:+ $HOSTLDFLAGS}"
export HOSTLDLIBS="-lelf${HOSTLDLIBS:+ $HOSTLDLIBS}"

exec gmake -C "$deps_dir/linux" "$@"
