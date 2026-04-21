# MLNIX Minimal Kernel Example

This example turns the old Minix stub into a tiny Linux-style teaching OS:

- freestanding AArch64 kernel
- PL011 serial console on QEMU `virt`
- minimal GNU-flavored shell prompt
- pseudo-files under `/`, `/etc`, and `/proc`
- most kernel logic written in MLang, including MMIO and CPU register access
  through inline AArch64 assembly

The image is still intentionally small and monolithic. It is not Linux, and it
does not implement user/kernel isolation, ELF loading, syscalls, or a real VFS.
It is a compact kernel-and-shell example that shows how to drive a QEMU guest
from `mlang.toml`.

## Commands

Inside the guest shell:

- `help`
- `uname -a`
- `ls /`
- `ls /proc`
- `ps`
- `mem`
- `cat /README`
- `cat /etc/motd`
- `cat /proc/version`
- `cat /proc/cpuinfo`
- `cat /proc/meminfo`
- `echo hello`
- `clear`
- `dmesg`

## Build And Run

From this directory:

```sh
../../build/mlang pkg run qemu-run
```

The package tasks:

1. emit LLVM IR from the MLang kernel
2. retarget that IR to `aarch64-none-elf`
3. compile the IR into an ELF object with `llc`
4. assemble/link the boot stub and runtime shim with `aarch64-none-elf-gcc`
5. flatten the image with `llvm-objcopy`
6. boot it in `qemu-system-aarch64`

## Host Tools

Expected tools in `PATH`:

- `../../build/mlang`
- `llc`
- `llvm-objcopy`
- `aarch64-none-elf-gcc`
- `qemu-system-aarch64`
