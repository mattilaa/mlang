# Path and Transitive Dependency Example

This example is a three-package MLang graph:

```text
path_dependency_app v1.0.0
`-- core ^1.2 (path packages/core) => v1.2.4
    `-- math ~2.1 (path ../math) => v2.1.3
```

`core` and `math` are independent MLang packages. Each builds its own dynamic
library in its own `build/` directory. The root executable links `core`, while
`core` links its transitive `math` dependency.

Run the complete inspection, lock, build, verification, and execution flow:

```sh
./run_demo.sh
```

Or run each command from this directory:

```sh
../../build/mlang pkg tree
../../build/mlang pkg why math
../../build/mlang pkg lock
../../build/mlang pkg build --locked
../../build/mlang pkg verify
./build/path_dependency_app
```

Expected program output:

```text
transitive path dependency result: 42
```
