# Dynamic Library Package Example

This example builds an MLang dynamic library and then links a runnable MLang
executable against it. Everything is declared in `mlang.toml`; no custom build
script is needed.

```toml
[[bin]]
name = "dynamic_library_demo"
entry = "src/main.mla"
depends_on = ["arithmetic"]

[[lib]]
name = "arithmetic"
entry = "src/arithmetic.mla"
```

Although the executable appears first in the manifest, `depends_on` makes the
package manager build `arithmetic` first. It also links the executable with the
library and adds a loader-relative runtime search path automatically.

The generated files are:

- macOS: `build/libarithmetic.dylib` and `build/dynamic_library_demo`
- Linux: `build/libarithmetic.so` and `build/dynamic_library_demo`
- Windows/MinGW: `build/arithmetic.dll`, its import library
  `build/libarithmetic.dll.a`, and `build/dynamic_library_demo`

MLang overloads have type-suffixed ABI symbols. For example, the library's
`add(i32, i32)` function is imported by the executable as
`extern fn add__i32_i32(...)`.

## Build and run

From this directory:

```sh
../../build/mlang pkg build
./build/dynamic_library_demo
```

Expected output:

```text
dynamic library results: sum=42, product=42
```

The compiler mode used by `[[lib]]` is also available directly:

```sh
../../build/mlang --shared src/arithmetic.mla -o build/libarithmetic.dylib
```

Use a `.so` output name on Linux or `.dll` on Windows.
