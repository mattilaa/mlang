# macOS PKG installer example

A minimal MLang executable packaged with its README and an uninstaller.
Requires macOS, Apple's command line developer tools (`pkgbuild`), and the
repository compiler and standard library built under `../../build`.

From this directory:

```sh
../../build/mlang pkg run package
pkgutil --payload-files build/hello_pkg-0.1.0.pkg
```

The `package` task depends on `compile`, then calls the reusable
`scripts/package_macos_project.sh` helper. No manifest schema extensions or
external dependencies are needed. Building does not install anything.
Existing output packages are refused; move the old package or change the
version/output filename before rebuilding. Keep the identifier stable across
upgrades and increase the version passed to the packaging script as well as
the package version in `mlang.toml`.

Install explicitly (or open the `.pkg` in Finder):

```sh
sudo /usr/sbin/installer -pkg build/hello_pkg-0.1.0.pkg -target /
/usr/local/lib/org.mlang.examples.hello-pkg/hello_pkg
```

The package is unsigned and contains the executable for the architecture of
the compiler used. For distribution, sign with a Developer ID Installer
identity using `productsign` and follow Apple's notarization requirements.
The helper does not bundle third-party dynamic libraries; standalone projects
must supply those separately or link them statically.

Preview and uninstall, even after deleting the source project:

```sh
/bin/bash /usr/local/lib/org.mlang.examples.hello-pkg/uninstall.sh
sudo /bin/bash /usr/local/lib/org.mlang.examples.hello-pkg/uninstall.sh --remove
```

`mlang pkg run uninstall-preview` runs the same preview from this project.
Removal deletes exactly the installed executable, README, and uninstaller
(including any edits to those three files), removes the install directory only
if empty, then forgets the installer receipt. Additional files are preserved.
`pkgutil --forget` alone does not delete installed files. The uninstaller does
not remove shared parent directories or follow symlinked parent directories.
Installation and removal require administrator privileges; packaging does not.

To reuse the helper for another single-executable project:

```sh
bash scripts/package_macos_project.sh path/to/executable README.md \
  my_app com.example.my-app 1.0.0 dist/my_app-1.0.0.pkg
```

It installs into `/usr/local/lib/com.example.my-app/`. Use a unique identifier
and reserve that directory and the three payload names for this package.
