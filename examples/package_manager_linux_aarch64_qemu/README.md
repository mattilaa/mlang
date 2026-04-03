# Linux AArch64 QEMU Package Example

This example demonstrates two new package-manager capabilities:

- fetch-only source dependencies with `build = "none"`
- shell-driven custom tasks via `[[task]]`, `env`, and `mlang pkg run`

The package fetches a Linux kernel tarball, builds an AArch64 kernel image with
the configured make tool, creates a tiny initramfs, and boots it under QEMU.

Linux is the recommended host for this example. macOS support is best-effort
and requires extra Homebrew host-tool setup for LLVM, `lld`, and `libelf`.

On macOS, the manifest prepends common Homebrew tool directories to `PATH` so
newer Homebrew `make` and other tools can be used instead of `/usr/bin`.

## Manifest highlights

```toml
[dependencies]
linux = { url = "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.1.tar.gz", archive = "tar.gz", strip_components = "1", build = "none" }

[[task]]
name = "kernel-build"
commands = [
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} defconfig",
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} -j${JOBS:-4} Image"
]

[task.host.darwin]
env = [
  "LLVM=1",
  "LLVM_IAS=1",
  "CC=/opt/homebrew/opt/llvm/bin/clang"
]

[tool.mlang]
make_program = "gmake"
path_entries = [
  "/opt/homebrew/opt/make/libexec/gnubin",
  "/usr/local/opt/make/libexec/gnubin",
  "/opt/homebrew/opt/lld/bin",
  "/usr/local/opt/lld/bin",
  "/opt/homebrew/opt/llvm/bin",
  "/usr/local/opt/llvm/bin",
  "/opt/homebrew/bin",
  "/usr/local/bin"
]

[[task]]
name = "qemu-run"
commands = [
  "qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024 -nographic -kernel {{deps_dir}}/linux/arch/arm64/boot/Image -initrd {{build_dir}}/initramfs.cpio.gz -append \"console=ttyAMA0 rdinit=/init\""
]
```

## Install

- `make`
- `gmake` on macOS if Homebrew `make` is installed and `make_program = "gmake"`
- Homebrew `llvm`
- Homebrew `lld`
- Homebrew `libelf`
- `cpio`
- `gzip`
- `qemu-system-aarch64`
- an AArch64-capable kernel toolchain

On macOS, install the toolchain first:

```sh
brew install llvm lld libelf make qemu cpio
```

On Debian/Ubuntu, install:

```sh
sudo apt-get install clang lld make qemu-system-arm cpio gzip gcc-aarch64-linux-gnu
```

On native AArch64 hosts, `CROSS_COMPILE` can usually be left empty.
On x86_64 Linux hosts, set something like:

```sh
export CROSS_COMPILE=aarch64-linux-gnu-
```

## Build And Run

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg run toolchain-check
../../build/mlang pkg run kernel-build
../../build/mlang pkg run initramfs
../../build/mlang pkg run qemu-run
```

To only compile the Linux kernel image for AArch64 without booting QEMU:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg run toolchain-check
../../build/mlang pkg run kernel-build
```

Or step-by-step:

```sh
../../build/mlang pkg run toolchain-check
../../build/mlang pkg run kernel-defconfig
../../build/mlang pkg run kernel-build
../../build/mlang pkg run initramfs
../../build/mlang pkg run qemu-run
```

## Notes

- `build = "none"` is important here because the Linux source tree is not built
  by the package manager's built-in `cmake` / `meson` / `make` dependency
  handlers.
- `make_program = "gmake"` lets the package pick the Homebrew GNU Make binary
  directly for tasks and built-in `build = "make"` dependency builds.
- `env = ["KEY=value"]` lets each task set toolchain variables such as
  `LLVM=1`, `LLVM_IAS=1`, and `CC` without hardcoding them into every command.
- `[task.host.darwin]` lets the manifest keep Linux as the simple default path
  while adding a macOS-specific task override only where it is needed.
- `{{make}}` expands to the configured make executable from
  `[tool.mlang].make_program`.
- `path_entries` in `mlang.toml` lets the package prepend Homebrew tool
  directories to `PATH`, which is useful on macOS when `/usr/bin/make` is too
  old for the kernel tree being built.
- The kernel build works on macOS by running the Linux kernel in LLVM mode
  (`LLVM=1`, `LLVM_IAS=1`) so it picks up Homebrew `clang` and `ld.lld`
  instead of Apple `/usr/bin/ld`.
- On macOS, the build tasks also resolve Homebrew `libelf`, export
  `PKG_CONFIG_PATH`, add host include/library flags, and generate a local
  `build/host-compat/elf.h` shim that maps the kernel's `<elf.h>` include to
  Homebrew `libelf/sys_elf.h`.
- `toolchain-check` fails early if the required Homebrew LLVM, `ld.lld`, or
  `libelf` metadata is missing.
- Even with those fixes, macOS remains best-effort. Linux is the more reliable
  host for full kernel builds and future kernel version changes.
- The current macOS path gets past the missing GNU Make, linker, and `elf.h`
  failures, but Linux host tools such as `scripts/sorttable` can still hit ABI
  mismatches against Homebrew `libelf`. Treat Linux as the supported host for
  reproducible full-kernel builds.
- The `{{root}}`, `{{build_dir}}`, and `{{deps_dir}}` placeholders are
  expanded by `mlang pkg run` before the shell commands execute.
- This example is intentionally task-driven. Its primary goal is orchestrating
  the Linux + QEMU flow through the package manager, not producing a native
  `mlang` executable via `pkg build`.
