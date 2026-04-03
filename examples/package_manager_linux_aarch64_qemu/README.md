# Linux AArch64 QEMU Package Example

This example demonstrates package-manager capabilities around:

- fetch-only source dependencies with `build = "none"`
- shell-driven custom tasks via `[[task]]`, `depends_on`, `parallel`, `shell`, and
  `mlang pkg run`

The package fetches a Linux kernel tarball, builds an AArch64 kernel image with
the configured make tool, creates a tiny initramfs, and boots it under QEMU.

Linux is the recommended host for this example. On macOS, the manifest uses a
Darwin-specific task override that builds the kernel inside Docker instead of
trying to compile Linux host tools natively.

## Manifest highlights

```toml
[dependencies]
linux = { url = "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.1.tar.gz", archive = "tar.gz", strip_components = "1", build = "none" }

[[task]]
name = "kernel-build"
shell = [
  "docker run --rm \\",
  "  -v {{root}}:/workspace \\",
  "  -w /workspace/build/deps/linux \\",
  "  -e CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-} \\",
  "  mlang-linux-kernel-aarch64-qemu:latest \\",
  "  sh -lc 'make ARCH=arm64 -j${JOBS:-4} Image'"
]

[task.host.linux]
commands = [
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} defconfig",
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE:-} -j${JOBS:-4} Image"
]

[[task]]
name = "qemu-run"
parallel = true
depends_on = ["kernel-build", "initramfs"]

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
- Docker Desktop or another Docker runtime on macOS
- `cpio`
- `gzip`
- `qemu-system-aarch64`
- an AArch64-capable kernel toolchain

On macOS, install:

```sh
brew install make qemu cpio
```

and install/start Docker Desktop.

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
../../build/mlang pkg run qemu-run
```

`qemu-run` now triggers `kernel-build` and `initramfs` through task
dependencies. With `parallel = true`, those prerequisite tasks can run
concurrently before QEMU starts.

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
- `depends_on = ["task-name"]` lets a task sequence prerequisite tasks such as
  `toolchain-check` and `docker-image`.
- `parallel = true` lets a task run its `depends_on` prerequisites
  concurrently. The task's own `commands` or `shell` still run sequentially.
- `shell = [ ... ]` lets the manifest define an inline shell script directly in
  TOML instead of shelling out to helper files.
- `[task.host.darwin]` and `[task.host.linux]` let the manifest keep Linux as
  the simple default path while using Docker specifically on macOS.
- `{{make}}` expands to the configured make executable from
  `[tool.mlang].make_program`.
- `path_entries` in `mlang.toml` lets the package prepend Homebrew tool
  directories to `PATH`, which is useful on macOS when `/usr/bin/make` is too
  old for the kernel tree being built.
- On macOS, `toolchain-check` verifies Docker availability, `docker-image`
  builds the Linux kernel builder image, and `kernel-build` runs inside that
  container. This avoids the native macOS `libelf`/kernel-host-tool mismatch.
- The `{{root}}`, `{{build_dir}}`, and `{{deps_dir}}` placeholders are
  expanded by `mlang pkg run` before the shell commands execute.
- This example is intentionally task-driven. Its primary goal is orchestrating
  the Linux + QEMU flow through the package manager, not producing a native
  `mlang` executable via `pkg build`.
