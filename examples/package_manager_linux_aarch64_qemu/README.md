# Linux AArch64 QEMU Package Example

This example demonstrates two new package-manager capabilities:

- fetch-only source dependencies with `build = "none"`
- shell-driven custom tasks via `[[task]]` and `mlang pkg run`

The package fetches a Linux kernel tarball, builds an AArch64 kernel image with
`make`, creates a tiny initramfs, and boots it under QEMU.

On macOS, the manifest prepends common Homebrew tool directories to `PATH` so
newer Homebrew `make` and other tools can be used instead of `/usr/bin`.

## Manifest highlights

```toml
[dependencies]
linux = { url = "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.1.tar.gz", archive = "tar.gz", strip_components = "1", build = "none" }

[[task]]
name = "kernel-build"
commands = [
  "make -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} defconfig",
  "make -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} -j${JOBS:-4} Image"
]

[tool.mlang]
path_entries = [
  "/opt/homebrew/opt/make/libexec/gnubin",
  "/usr/local/opt/make/libexec/gnubin",
  "/opt/homebrew/bin",
  "/usr/local/bin"
]

[[task]]
name = "qemu-run"
commands = [
  "qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024 -nographic -kernel {{deps_dir}}/linux/arch/arm64/boot/Image -initrd {{build_dir}}/initramfs.cpio.gz -append \"console=ttyAMA0 rdinit=/init\""
]
```

## Prereqs

- `make`
- `cpio`
- `gzip`
- `qemu-system-aarch64`
- an AArch64-capable kernel toolchain

On native AArch64 hosts, `CROSS_COMPILE` can usually be left empty.
On x86_64 Linux hosts, set something like:

```sh
export CROSS_COMPILE=aarch64-linux-gnu-
```

## Run

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg run kernel-build
../../build/mlang pkg run initramfs
../../build/mlang pkg run qemu-run
```

Or step-by-step:

```sh
../../build/mlang pkg run kernel-defconfig
../../build/mlang pkg run kernel-build
../../build/mlang pkg run initramfs
../../build/mlang pkg run qemu-run
```

## Notes

- `build = "none"` is important here because the Linux source tree is not built
  by the package manager's built-in `cmake` / `meson` / `make` dependency
  handlers.
- `path_entries` in `mlang.toml` lets the package prepend Homebrew tool
  directories to `PATH`, which is useful on macOS when `/usr/bin/make` is too
  old for the kernel tree being built.
- The `{{root}}`, `{{build_dir}}`, and `{{deps_dir}}` placeholders are
  expanded by `mlang pkg run` before the shell commands execute.
- This example is intentionally task-driven. Its primary goal is orchestrating
  the Linux + QEMU flow through the package manager, not producing a native
  `mlang` executable via `pkg build`.
