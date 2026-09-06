# Explicit Package Includes

This example uses root `[[include]]` entries to select independent package
manifests without recursively discovering every `mlang.toml` in the tree.
Each entry has a required, unique output target:

```toml
[[include]]
path = "apps/editor"
target = "editor"

[[include]]
path = "tools/converter/mlang.toml"
target = "converter"
```

A directory `path` resolves to its `mlang.toml`; a manifest file can also be
named directly. The child packages retain their own source roots, dependencies,
tasks, and target declarations. Their artifacts and fetched dependencies are
isolated beneath the coordinator's build directory:

```text
build/
├── editor/
│   ├── editor
│   ├── asset-compiler
│   └── deps/
└── converter/
    ├── converter
    └── deps/
```

From this directory, run:

```sh
../../build/mlang pkg build
./build/editor/editor
./build/editor/asset-compiler
./build/converter/converter
```

Or run the complete build and verification:

```sh
./run_demo.sh
```

Expected program output:

```text
editor package built in its include target
asset compiler shares the editor output namespace
converter package built separately
```
