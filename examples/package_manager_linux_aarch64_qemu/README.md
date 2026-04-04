# Linux AArch64 QEMU Package Example

This example demonstrates package-manager capabilities around:

- fetch-only source dependencies with `build = "none"`
- shell-driven custom tasks via `[[task]]`, `depends_on`, `join_on`, `next`,
  `parallel`, `shell`, and `mlang pkg run`

The package fetches a Linux kernel tarball, builds an AArch64 kernel image with
the configured make tool, creates a tiny initramfs, and boots it under QEMU.

Linux is still the recommended host for this example. On Apple Silicon macOS,
the manifest also provides a native Darwin path based on the ClangBuiltLinux
workflow described at <https://seiya.me/blog/building-linux-on-macos-natively>.
That path uses Homebrew LLVM, `libelf`, GNU `sed`, generated compatibility
headers, and a small `file2alias.c` patch for kernel host tools.

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
depends_on = ["darwin-native-prepare"]
env = [
  "HOSTCFLAGS=-Iscripts/macos-include -I/opt/homebrew/opt/libelf/include -I/usr/local/opt/libelf/include -Wno-error=incompatible-pointer-types",
  "HOSTLDFLAGS=-L/opt/homebrew/opt/libelf/lib -L/usr/local/opt/libelf/lib",
  "HOSTLDLIBS=-lelf"
]
commands = [
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 LLVM=1 LLVM_IAS=1 defconfig",
  "{{make}} -C {{deps_dir}}/linux ARCH=arm64 LLVM=1 LLVM_IAS=1 -j${JOBS:-4} Image"
]

[[task]]
name = "boot-flow"
parallel = true
next = ["kernel-build", "initramfs", "qemu-run"]

[[task]]
name = "qemu-run"
join_on = ["kernel-build", "initramfs"]

[tool.mlang]
make_program = "gmake"
path_entries = [
  "/opt/homebrew/opt/gnu-sed/libexec/gnubin",
  "/usr/local/opt/gnu-sed/libexec/gnubin",
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
shell = [
  "exec qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024 -nographic -kernel {{deps_dir}}/linux/arch/arm64/boot/Image -initrd {{build_dir}}/initramfs.cpio.gz -append 'console=ttyAMA0 rdinit=/init'"
]
```

## Install

- `make`
- `gmake` on macOS if Homebrew `make` is installed and `make_program = "gmake"`
- GNU `sed` on macOS
- `llvm`, `lld`, and `libelf` on macOS
- `cpio`
- `gzip`
- `qemu-system-aarch64`
- an AArch64-capable kernel toolchain

On Apple Silicon macOS, install:

```sh
brew install llvm lld libelf gnu-sed make qemu cpio
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
../../build/mlang pkg run boot-flow
```

`boot-flow` fans out into `kernel-build`, `initramfs`, and `qemu-run`.
Because `qemu-run` declares `join_on = ["kernel-build", "initramfs"]`, it
waits until the kernel image and initramfs are both ready. With
`parallel = true`, the independent branches can run concurrently before QEMU
starts.

To only compile the Linux kernel image for AArch64 without booting QEMU:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
```

For this example, `pkg build` runs tasks tagged with `phase = "build"`, which
maps to the `kernel-build` task.

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
  `toolchain-check` and `darwin-native-prepare`.
- `next = ["task-name"]` lets a task jump forward to named downstream tasks
  after its own commands succeed.
- `join_on = ["task-a", "task-b"]` lets a task wait for named tasks, which is
  the clean way to join parallel branches.
- `parallel = true` lets a task run its `depends_on` prerequisites
  concurrently, and also allows multiple `next` tasks to run concurrently. The
  task's own `commands` or `shell` still run sequentially.
- `shell = [ ... ]` lets the manifest define an inline shell script directly in
  TOML instead of shelling out to helper files.
- `[task.host.darwin]` and `[task.host.linux]` let the manifest keep Linux as
  the simple default path while using native Apple Silicon overrides on macOS.
- `{{make}}` expands to the configured make executable from
  `[tool.mlang].make_program`.
- `path_entries` in `mlang.toml` lets the package prepend Homebrew tool
  directories to `PATH`, which is useful on macOS when `/usr/bin/make` is too
  old for the kernel tree being built.
- On macOS, `toolchain-check` verifies that Homebrew LLVM, GNU `sed`, and
  `libelf` are present. `darwin-native-prepare` then generates
  `scripts/macos-include/elf.h`, `scripts/macos-include/byteswap.h`, and
  applies the `file2alias.c` workaround before running the native kernel build
  with `LLVM=1`. The Darwin host flags also add
  `-Wno-error=incompatible-pointer-types` to get past known Homebrew
  `libelf` typedef mismatches in kernel host tools such as `scripts/sorttable`.
  If an earlier broken `file2alias.c` patch was already written into the fetched
  kernel tree, rerun `../../build/mlang pkg run darwin-native-prepare` once to
  repair it in place before retrying `kernel-build`.
- This native Darwin path is based on the approach documented by Seiya Nuta:
  <https://seiya.me/blog/building-linux-on-macos-natively>
- The `{{root}}`, `{{build_dir}}`, and `{{deps_dir}}` placeholders are
  expanded by `mlang pkg run` before the shell commands execute.
- This example is intentionally task-driven. Its primary goal is orchestrating
  the Linux + QEMU flow through the package manager, not producing a native
  `mlang` executable via `pkg build`.
