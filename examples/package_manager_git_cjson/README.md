# Package Manager + Git C Dependency (cJSON)

This example shows how to declare a **git-based C dependency** in `mlang.toml`,
fetch it with `mlang pkg fetch`, and link it when building.

## What `pkg fetch` does here
- `mlang pkg fetch` **clones git dependencies** listed under `[dependencies]`.
- For this example, it clones `cJSON` into `build/deps/cjson`.
- This is different from `[c-dependencies]` (system libraries), which are not
  fetched and are resolved via `pkg-config` at build time.

## Static linking via CMake options
This example passes CMake options via `cmake_args` to build a **static** cJSON
library:

```toml
cjson = { git = "https://github.com/DaveGamble/cJSON.git", build = "cmake", cmake_args = "CMAKE_POLICY_VERSION_MINIMUM=3.5;BUILD_SHARED_LIBS=OFF;CJSON_BUILD_SHARED_LIBS=OFF;CJSON_BUILD_STATIC_LIBS=ON" }
```

The package manager forwards `cmake_args` entries to CMake as `-D` definitions.

## Also uses a system dependency (libcurl)
This example additionally links against libcurl via pkg-config and uses it to
fetch a URL. By default it fetches `https://www.google.com`, or you can pass
`--url <site>` to choose another target. libcurl writes the response body to
stdout by default (since no custom write callback is provided).
The `curl_easy_setopt` function is variadic, so the extern signature uses `...`.
Common libcurl externs are collected in `curl.mla` and imported by `src/main.mla`.
`mlang.toml` includes `module_paths = ["."]` so the module loader can find
`curl.mla` from `src/main.mla`. It also shows package build defaults in
`[tool.mlang]`, including `opt_level` and `target_arch`.

## Prereqs
- `mlang` on your PATH
- `git`
- `cmake`
- A C toolchain (`cc`/`clang`/`gcc`)
- `pkg-config` and `libcurl` development package

### Install hints
macOS (Homebrew):
```sh
brew install git cmake pkg-config curl
export PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
```

Homebrew installs curl as a keg-only formula. Keep the `PKG_CONFIG_PATH`
setting in the shell where you run `mlang pkg build` so `pkg-config` can find
`libcurl.pc`.

Ubuntu/Debian:
```sh
sudo apt-get update
sudo apt-get install -y git cmake build-essential pkg-config libcurl4-openssl-dev
```

Fedora:
```sh
sudo dnf install -y git cmake gcc gcc-c++ make pkgconf-pkg-config libcurl-devel
```

Arch:
```sh
sudo pacman -S --needed git cmake base-devel pkgconf curl
```

## Build and run
From this directory:

```sh
mlang pkg fetch
mlang pkg build
# Optional optimization level:
# mlang pkg build -O3
./build/cjson_demo
./build/cjson_demo --url https://www.someplace.com
./build/cjson_demo --help
mlang pkg clean
```

This example currently sets these build defaults in `mlang.toml`:

```toml
[tool.mlang]
module_paths = ["."]
opt_level = "O2"
target_arch = "aarch64"
```

`mlang pkg build -O3` still overrides `opt_level` from the manifest.

## Notes
- You can pin a specific revision or tag in `mlang.toml` with `rev` or `tag`.
- The package manager now adds rpaths for dependency build folders so dylibs
  are found at runtime without extra env vars (macOS/Linux).
- For C APIs that return C strings, use `str8` in your extern signatures.
- libcurl response output is printed directly by libcurl during the request.
