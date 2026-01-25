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
fetch `https://www.google.com`. libcurl writes the response body to stdout by
default (since no custom write callback is provided).
The `curl_easy_setopt` function is variadic, so the extern signature uses `...`.

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
```

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
./build/cjson_demo
```

## Notes
- You can pin a specific revision or tag in `mlang.toml` with `rev` or `tag`.
- The package manager now adds rpaths for dependency build folders so dylibs
  are found at runtime without extra env vars (macOS/Linux).
- For C APIs that return C strings, use `str8` in your extern signatures.
- libcurl response output is printed directly by libcurl during the request.
