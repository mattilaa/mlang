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

The demo also exercises reproducible dependency locking. `pkg lock` records
the exact Git commit and the downloaded archive's SHA-256 in the shared root
`mlang.lock`; `--locked` prevents a stale lock from being changed, and
`pkg verify` checks the fetched sources against it.

## Included packages

- `packages/git_cjson_demo`
  Fetches `cJSON` directly from GitHub using `git = "..."`
- `packages/tarball_cjson_demo`
  Fetches the same dependency from a GitHub release/source tarball using
  `url = "...tar.gz"`

Both packages build a small executable that parses a JSON string with cJSON.
The `tarball_cjson_demo` package is a real runtime demo, not just a fetch-only
fixture: it links against the archive-fetched cJSON library and uses it to
parse JSON at runtime.

## Run

From this directory:

```sh
mlang pkg lock
mlang pkg build --locked
mlang pkg verify
./packages/git_cjson_demo/build/git_cjson_demo
./packages/tarball_cjson_demo/build/tarball_cjson_demo
mlang pkg clean
```

Or run the helper script:

```sh
./run_demo.sh
```

## Notes

- The workspace root itself is not a package. It only declares `[workspace]`.
- Each discovered subpackage keeps its own local `build/` directory.
- Both workspace members share the root `mlang.lock`.
- After one successful online fetch, `mlang pkg fetch --offline` reuses the
  locked Git checkout and cached archive without contacting their origins.
- The tarball package uses:

```toml
cjson = {
  url = "https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz",
  archive = "tar.gz",
  strip_components = "1",
  build = "cmake"
}
```

- The tarball package executable prints a parsed value from JSON using the
  archive-fetched cJSON build, so the fetch/build/run path is exercised
  end-to-end.
