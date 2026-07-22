# std::term

Module file: `stdlib/std/term.mla`

Terminal capability detection and termios helpers.

### Types
- `TermSize`
- `TerminalCaps`
- `ColorLevel` (`i32` alias)

### Color level constants
- `color_none() -> ColorLevel` (0)
- `color_16() -> ColorLevel` (16)
- `color_256() -> ColorLevel` (256)
- `color_truecolor() -> ColorLevel` (16777216)

### Capability queries
- `term_name() -> str8`
- `supports_ansi() -> i32`
- `stdin_is_tty() -> i32`
- `stdout_is_tty() -> i32`
- `stderr_is_tty() -> i32`
- `stdout_size() -> TermSize`
- `stderr_size() -> TermSize`
- `stdout_color_level() -> ColorLevel`
- `stderr_color_level() -> ColorLevel`
- `stdout_truecolor() -> i32`
- `stderr_truecolor() -> i32`
- `stdout_caps() -> TerminalCaps`
- `stderr_caps() -> TerminalCaps`

### termios helpers
- `stdin_enable_raw() -> i32`
- `stdin_restore() -> i32`
