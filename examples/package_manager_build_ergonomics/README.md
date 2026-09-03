# Package Manager Build Ergonomics

This workspace demonstrates named build profiles, opt-in features and optional
dependencies, selecting one package with `-p`, the content-addressed global
artifact cache, and vendoring a locked transitive dependency graph for an
offline build.

Run the complete demonstration from the repository root:

```sh
./examples/package_manager_build_ergonomics/run_demo.sh
```

It runs `ergonomic_app` twice in the `dev` profile (the second build reports
global cache hits), vendors its enabled dependency, rebuilds and runs it offline
in `release`, then selects and runs the separate `utility_app` package.

