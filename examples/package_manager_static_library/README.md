# Static Library Package Example

This package builds MLang code into `build/libarithmetic_static.a`, then links
that archive into an MLang executable. The finished executable has no runtime
dependency on the internal library.

```toml
[[bin]]
name = "static_library_demo"
entry = "src/main.mla"
depends_on = ["arithmetic_static"]

[[lib]]
name = "arithmetic_static"
entry = "src/arithmetic.mla"
type = "static"
```

`depends_on` controls build order and passes the generated archive directly to
the executable link step.

## Build, run, and verify

From this directory:

```sh
../../build/mlang pkg build
test -f build/libarithmetic_static.a
./build/static_library_demo
```

Expected output:

```text
static library results: difference=42, square=49
```

Inspect the archive members with:

```sh
ar -t build/libarithmetic_static.a
```

Confirm that the executable has no dynamic dependency on the internal static
library:

```sh
if command -v otool >/dev/null 2>&1; then
  ! otool -L build/static_library_demo | grep -q arithmetic_static
elif command -v ldd >/dev/null 2>&1; then
  ! ldd build/static_library_demo | grep -q arithmetic_static
fi
```

The underlying compiler mode can also be used directly:

```sh
../../build/mlang --static-library src/arithmetic.mla -o build/libarithmetic_static.a
```
