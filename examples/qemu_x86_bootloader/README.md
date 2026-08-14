# MLang x86 BIOS bootloader example

This example builds a two-stage legacy x86 BIOS disk image entirely from MLang
sources. The 512-byte loader in `boot.mla` reads the separate `kernel.mla`
image from disk sectors 2-33 into physical address `0x10000`, then transfers
control to it. The kernel switches to 32-bit protected mode and enters the
ordinary MLang `terminal_main()` function. Architecture-qualified module
assembly defines the hardware entry and I/O primitives that cannot live in a
normal function:

```mla
asm x86(".code16
.globl _start
_start:
    // real-mode assembly
");
```

The boot sector initializes its real-mode segments and stack, preserves the
BIOS boot-drive number, and loads the kernel with the BIOS extended LBA read.
The kernel installs a Global Descriptor Table, enters protected mode,
initializes COM1, loads MFS2 through ATA PIO, mounts it at `/`, and provides a
line-oriented terminal over QEMU's serial console. The build checks the loader
size, `55 aa` signature, kernel size, configured filesystem size, and final IDE
disk image size.

## Requirements

QEMU must be installed. The build also needs LLVM `clang`, `ld.lld`, and
`llvm-objcopy`, plus a built MLang compiler at `../../build/mlang`.

macOS with Homebrew:

```sh
brew install qemu llvm lld
```

Debian or Ubuntu:

```sh
sudo apt install qemu-system-x86 clang lld llvm
```

## Build and run with mlang.toml

```sh
cd examples/qemu_x86_bootloader
../../build/mlang pkg run demo
```

The default MFS2 capacity is 1 MiB. Override it in KiB for either `build` or
`demo`:

```sh
../../build/mlang pkg run demo --option filesystem_kib=65536
```

This example creates a 64 MiB filesystem. Supported values range from 4 KiB
through 1 GiB. The run script derives the QEMU memory allocation from this
value so the kernel has room for both MFS2 and the editor workspace.

Expected output:

```text
MLang bootloader: loading kernel...
MLang bootloader: kernel loaded.

MLang virtual terminal
Mounted writable MFS2 at /.
Type 'help' for available commands.

mlang>
```

The terminal supports:

- `help`: list commands
- `about`: show kernel information
- `clear`: clear the serial terminal with ANSI control sequences
- `pwd`: print the current working directory
- `ls [path]`: list direct children of a directory
- `cd <path>`: change the current working directory; `cd ..` is supported
- `cat <path>`: print a file's contents
- `touch <path>`: create and persist an empty file
- `vi <path>` or `/bin/vi <path>`: replace and persist a text file's contents
- `sync`: flush the used MFS2 data and metadata to the IDE disk image
- `reboot`: reset the virtual machine through the keyboard controller
- `halt`: halt the virtual CPU

Printable input and backspace editing are supported. Press `Ctrl-c` to stop
QEMU after using `halt`.

Build without starting QEMU:

```sh
../../build/mlang pkg run build
```

The scripts expose the same option directly:

```sh
./build.sh --filesystem-kib 16384
./run.sh --filesystem-kib 16384
```

The generated boot-sector, protected-mode kernel, filesystem, ELF, and final
`disk.img` files are written under `build/`.

## MFS2 filesystem

`filesystem.mla` defines the initial hierarchical filesystem contents. The
build pads that seed to the configured capacity and writes it from LBA 36 of
the IDE disk. The protected-mode kernel reads the used sectors into physical
address `0x100000`, validates the `MFS2` header, and mounts it at `/`.

The initial tree is:

```text
/
|-- bin/
|   `-- vi
|-- etc/
|   `-- motd
|-- home/
|   `-- user/
|       `-- readme.txt
`-- tmp/
    `-- example.txt
```

For example:

```text
mlang> cd /home/user
mlang> pwd
/home/user
mlang> cat readme.txt
This file lives in /home/user on the MFS2 root filesystem.
mlang> touch session.log
mlang> ls
readme.txt
session.log
mlang> vi notes.txt
MLang vi: /home/user/notes.txt
--- replace contents; '.' saves, ':q!' cancels ---
| first line
| second line
| .
vi: saved
mlang> cat notes.txt
first line
second line
```

The editor is intentionally line-oriented rather than a full-screen terminal
editor. Starting it replaces the file contents: enter one text line at a time,
then enter `.` alone to save or `:q!` alone to discard the edit. Canceling a
new file does not create it. Input lines support backspace through the serial
terminal's ordinary line editor.

Each fixed-size directory entry stores a 32-byte absolute path, entry type,
data offset, size, and capacity. The image reserves 32 directory slots.
`touch` allocates an entry in the mounted RAM copy. On save, the editor reserves
exactly the file's required bytes from the remaining data area. Later saves
overwrite that allocation when they fit or append a larger allocation when
needed. Both commands use the kernel's protected-mode ATA PIO driver to write
the contiguous used MFS2 sectors back to `disk.img`. `sync` exposes the same
flush operation explicitly; unused configured capacity is not transferred.

Created files survive the kernel's `reboot` command. The ordinary `demo` task
rebuilds the baseline disk before QEMU starts. To boot the existing image and
retain changes across QEMU process restarts, use:

```sh
../../build/mlang pkg run resume
```

The `resume` task requires an existing `build/disk.img`; run the `build` or
`demo` task once first. Re-running `build` resets the image to the files defined
in `filesystem.mla`. Disk images produced by the earlier floppy-backed version
are not compatible with the ATA layout; rebuild once before using `resume`.

There is no separate per-file size setting or fixed editor limit. A file may
grow until the configured MFS2 data area runs out of free bytes. The serial
line reader still accepts at most 63 printable characters per input line, but
the editor can collect as many lines as fit in the image. Because this version
has no compaction, growing an existing allocation leaves its smaller old block
unused. It also has no deletion, dynamic directory creation, or permissions.

## Module assembly

Top-level `asm <arch>("...");` emits text into LLVM's module assembly instead
of placing it inside a compiler-generated function. The architecture qualifier
is mandatory and must match `--target-arch`. Module assembly has no operands or
result value; use ordinary inline `asm` inside MLang functions for those cases.
