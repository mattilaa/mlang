# macOS Stdio Driver Demo

This is a macOS-oriented user-space driver-style demo built with MLang.

It is not a real DriverKit or kernel extension driver. Instead, it behaves
like a small device service controlled over standard input and output, with
shell scripts to attach and detach it.

The protocol is more device-like than the plain stdio demo:
- power and fault state control
- four registers exposed as `0..3`
- register read/write commands
- register dump output

## Files

- `main.mla`
- `attach_driver.sh`
- `detach_driver.sh`
- `send_command.sh`

## Usage

From the repo root:

```sh
./examples/macos_stdio_driver/attach_driver.sh
./examples/macos_stdio_driver/send_command.sh status
./examples/macos_stdio_driver/send_command.sh on
./examples/macos_stdio_driver/send_command.sh "write 0 0x2a"
./examples/macos_stdio_driver/send_command.sh "read 0"
./examples/macos_stdio_driver/send_command.sh dump
./examples/macos_stdio_driver/send_command.sh fault
./examples/macos_stdio_driver/send_command.sh clear
./examples/macos_stdio_driver/send_command.sh quit
./examples/macos_stdio_driver/detach_driver.sh
```

Runtime files are created under:

```sh
/tmp/mlang_macos_stdio_driver
```

## Commands

- `on`
- `off`
- `fault`
- `clear`
- `status`
- `ping`
- `read <index>`
- `write <index> <value>`
- `dump`
- `quit`
- `detach`

## Register Map

- `0` = `control`
- `1` = `status`
- `2` = `data`
- `3` = `error`

Values may be written in decimal or hex form, for example:

```sh
./examples/macos_stdio_driver/send_command.sh "write 2 123"
./examples/macos_stdio_driver/send_command.sh "write 2 0x7b"
```
