# Workspace Package Manager Example

This example demonstrates two new package-manager features together:

- workspace-style recursive subdirectory discovery from a root `mlang.toml`
- source dependencies fetched either from Git or from a `tar.gz` URL

The root manifest uses:

```toml
[workspace]
members = ["packages"]
```

`mlang pkg fetch` and `mlang pkg build` run from the workspace root and
discover package manifests recursively under `packages/`.

## Included packages

- `packages/git_cjson_demo`
  Fetches `cJSON` directly from GitHub using `git = "..."`
- `packages/tarball_cjson_demo`
  Fetches the same dependency from a GitHub release/source tarball using
  `url = "...tar.gz"`

Both packages build a small executable that parses a JSON string with cJSON.

## Run

From this directory:

```sh
../../build/mlang pkg fetch
../../build/mlang pkg build
./packages/git_cjson_demo/build/git_cjson_demo
./packages/tarball_cjson_demo/build/tarball_cjson_demo
../../build/mlang pkg clean
```

## Notes

- The workspace root itself is not a package. It only declares `[workspace]`.
- Each discovered subpackage keeps its own local `build/` directory.
- The tarball package uses:

```toml
cjson = {
  url = "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz",
  archive = "tar.gz",
  strip_components = "1",
  build = "cmake"
}
```
