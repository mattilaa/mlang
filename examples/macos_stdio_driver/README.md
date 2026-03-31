# macOS Stdio Driver Demo

This is a macOS-oriented user-space driver-style demo built with MLang.

It is not a real DriverKit or kernel extension driver. Instead, it behaves
like a small device service controlled over standard input and output, with
shell scripts to attach and detach it.

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
- `quit`
- `detach`
