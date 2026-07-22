# std::platform

Module file: `stdlib/std/platform.mla`

Multiplatform detection helpers built on compiler-recognized platform macros.

### Builtin macros
- `windows!() -> bool`
- `posix!() -> bool`
- `linux!() -> bool`
- `macos!() -> bool`
- `x64!() -> bool`
- `aarch64!() -> bool`

These macros evaluate as compile-time booleans, so they can be used inside
`static_assert!` and ordinary `if` branches.

For compile-time arch-specific function bodies, MLang also supports:
- `#[x86-64]`
- `#[aarch64]`

### Helper functions
- `is_windows() -> bool`
- `is_posix() -> bool`
- `is_linux() -> bool`
- `is_macos() -> bool`
