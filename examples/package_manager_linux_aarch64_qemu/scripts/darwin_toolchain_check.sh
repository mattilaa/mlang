#!/bin/sh
set -eu

if ! command -v gmake >/dev/null 2>&1; then
  echo "Missing required tool in PATH: gmake"
  echo "On macOS install Homebrew llvm lld libelf make qemu cpio"
  exit 1
fi

for tool in \
  /opt/homebrew/opt/llvm/bin/clang \
  /opt/homebrew/opt/llvm/bin/clang++ \
  /opt/homebrew/opt/llvm/bin/llvm-ar \
  /opt/homebrew/opt/llvm/bin/llvm-nm \
  /opt/homebrew/opt/llvm/bin/llvm-objcopy \
  /opt/homebrew/opt/llvm/bin/llvm-ranlib \
  /opt/homebrew/opt/llvm/bin/llvm-strip \
  /opt/homebrew/opt/lld/bin/ld.lld
do
  if [ ! -x "$tool" ]; then
    echo "Missing required tool: $tool"
    echo "On macOS install Homebrew llvm lld libelf make qemu cpio"
    exit 1
  fi
done

libelf_prefix="$(brew --prefix libelf 2>/dev/null || true)"
if [ -z "$libelf_prefix" ] || [ ! -f "$libelf_prefix/lib/pkgconfig/libelf.pc" ]; then
  echo "Missing required Homebrew libelf metadata. Install with: brew install libelf"
  exit 1
fi
