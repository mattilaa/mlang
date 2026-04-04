# Linux AArch64 QEMU Package Example

This example demonstrates package-manager capabilities around:

- fetch-only source dependencies with `build = "none"`
- shell-driven custom tasks via `[[task]]`, `depends_on`, `join_on`, `next`,
  `parallel`, `shell`, and `mlang pkg run`

The package fetches a Linux kernel tarball, builds an AArch64 kernel image with
the configured make tool, then packs one of two userspaces into the initramfs
directly from `mlang.toml`:

- a minimal BusyBox userspace
- a wider GNU-style userspace based on an Ubuntu Base ARM64 rootfs with
  `bash` and the standard GNU/Linux userland tools shipped there

Both modes boot through BusyBox `init` on the QEMU serial console. The
selected guest userspace comes from `[tool.mlang.options] userspace` and can be
overridden from the CLI with `--option userspace=...`.
The Linux dependency sets `spinner = false` so `curl` can display its own
download progress bar cleanly during `pkg fetch`. Other package-manager
operations keep the rolling spinner by default unless CLI log routing is
enabled.

Linux is still the recommended host for this example. On Apple Silicon macOS,
the manifest also provides a native Darwin path based on the ClangBuiltLinux
workflow described at <https://seiya.me/blog/building-linux-on-macos-natively>.
That path uses Homebrew LLVM, `libelf`, GNU `sed`, generated compatibility
headers, and a small `file2alias.c` patch for kernel host tools.

## Manifest highlights

```toml
[dependencies]
linux = { url = "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.1.tar.gz", archive = "tar.gz", strip_components = "1", build = "none", spinner = false }

[tool.mlang]
log_dir = "/tmp/mlang-linux-aarch64-qemu"
stdout_log = "pkg.stdout.log"
stderr_log = "pkg.stderr.log"
warn_log = "pkg.warn.log"

[tool.mlang.options]
userspace = "busybox"

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
name = "busybox-fetch"
commands = [
  "mkdir -p {{build_dir}}",
  "sh -c '[ -x {{build_dir}}/busybox-armv8l ] || curl -L --fail https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv8l -o {{build_dir}}/busybox-armv8l'",
  "chmod +x {{build_dir}}/busybox-armv8l"
]

[[task]]
name = "gnu-userspace-fetch"
commands = [
  [
    "sh",
    "-c",
    "if [ \"{{option.userspace}}\" != \"gnu\" ]; then exit 0; fi"
  ],
  [
    "sh",
    "-c",
    "[ -f {{build_dir}}/ubuntu-base-24.04.3-base-arm64.tar.gz ] || curl -L --fail https://cdimage.ubuntu.com/ubuntu-base/releases/noble/release/ubuntu-base-24.04.3-base-arm64.tar.gz -o {{build_dir}}/ubuntu-base-24.04.3-base-arm64.tar.gz"
  ],
  [
    "tar",
    "-xzf",
    "{{build_dir}}/ubuntu-base-24.04.3-base-arm64.tar.gz",
    "-C",
    "{{build_dir}}/gnu-rootfs"
  ]
]

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

Run the default BusyBox userspace explicitly with:

```sh
../../build/mlang pkg run qemu-run --option userspace=busybox
```

Run the wider GNU userspace with:

```sh
../../build/mlang pkg run qemu-run --option userspace=gnu
```

In GNU mode, the initramfs builder writes `/bin/guest-login` and starts QEMU at
a real serial login prompt instead of dropping directly into a root shell. The
default demo accounts are:

- `admin` / `admin`
- `user` / `user`
- `root` / `root`

The GNU login flow also writes a shared profile with `LS_COLORS`, `ls`, `ll`,
and `la` aliases, then places that profile into `/root`, `/home/admin`, and
`/home/user`. After a successful login, the session starts with the matching
`HOME` value and changes into that user-specific home directory.

The initramfs builder also seeds `/etc/passwd`, `/etc/group`, and
`/etc/shadow`, then installs small helper commands so these work in both
BusyBox and GNU mode:

- `addgroup GROUP [GID]`
- `adduser USER [GROUP]`
- `passwd [USER]`
- `sudo COMMAND ...`

This `sudo` implementation is intentionally minimal: the demo boots straight
into a constrained initramfs environment, so `sudo` simply re-executes the
command when already root and otherwise forwards to `su root -c ...`.

Example guest session:

```sh
login: admin
Password: admin
pwd
sudo ls --color=auto /
addgroup demo
adduser alice demo
passwd alice
grep '^alice:' /etc/passwd
grep '^demo:' /etc/group
su alice
pwd
```

That logs into the GNU guest as the default admin user, verifies the session
home directory, runs a root command through the minimal `sudo` wrapper, creates
a demo group, adds a user called `alice`, sets a password entry for that user
in `/etc/shadow`, verifies the generated account records, and finally shows how
to switch into the newly created user account. `adduser` now creates
`/home/<user>` automatically and populates a basic `.profile` there.

Both commands automatically fetch the Linux source dependency first. In GNU
mode, `gnu-userspace-fetch` also downloads and unpacks the official Ubuntu Base
ARM64 rootfs on demand before the initramfs is packed. The example uses Ubuntu
Base here because its tarball extracts cleanly on case-insensitive macOS
filesystems, unlike the Arch Linux ARM rootfs layout that hit terminfo
hard-link collisions.

By default, command output stays on the console even though this manifest
declares log file paths. To actually write package logs under
`/tmp/mlang-linux-aarch64-qemu/`, pass a pkg log flag such as `--stdout-log`,
`--stderr-log`, `--warn-log`, `--log-dir`, or
`--task-print-to-stdout-log`. Task `print` lines stay visible on the console,
and `--task-print-to-stdout-log` also mirrors them into the stdout log.
If you pass only `--log-dir`, pkg uses the default filenames
`pkg.stdout.log`, `pkg.stderr.log`, and `pkg.warn.log` in that directory.
The `qemu-run` task sets `log_output = false`, so QEMU's live serial output
stays on the console instead of being redirected into the package log files.

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
../../build/mlang pkg fetch
../../build/mlang pkg run toolchain-check
../../build/mlang pkg run kernel-defconfig
../../build/mlang pkg run kernel-build
../../build/mlang pkg run initramfs
../../build/mlang pkg run qemu-run
```

Running `../../build/mlang pkg run qemu-run` directly also works after
`pkg fetch`. Because `qemu-run` declares
`join_on = ["kernel-build", "initramfs"]`, the package manager runs those
tasks first and only starts QEMU after both succeed.

## Notes

- `build = "none"` is important here because the Linux source tree is not built
  by the package manager's built-in `cmake` / `meson` / `make` dependency
  handlers.
- `initramfs` is self-contained and creates a minimal directory tree under
  `{{build_dir}}/initramfs`, so the example does not rely on a checked-in
  `rootfs/` directory.
- `busybox-fetch` downloads the prebuilt
  `busybox-armv8l` binary from BusyBox's multiarch musl builds and installs it
  into the initramfs as `/bin/busybox`. The manifest then uses BusyBox as the
  real `/init`, creates `/bin/sh` and other applet symlinks for the BusyBox
  mode, and writes `/etc/inittab` so BusyBox `init` respawns a shell on
  `ttyAMA0`. This works on the QEMU guest because the kernel reports 32-bit
  EL0 support during boot.
- `gnu-userspace-fetch` optionally downloads an Ubuntu Base ARM64 rootfs
  tarball when `userspace=gnu`. `initramfs` overlays that rootfs into the
  generated image so the guest gets `/bin/bash` and a much wider GNU-style
  userspace than the minimal BusyBox mode.
- The resulting guest boots to `/ #` in BusyBox mode and to a GNU `bash`
  login shell in GNU mode.
- The tested BusyBox image does not include a `poweroff` applet, so exit QEMU
  with the `Ctrl-a x` nographic shortcut when needed.
- `log_output = false` can be set on an interactive task such as `qemu-run` to
  keep the child process on the console even when package logs are enabled.
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
