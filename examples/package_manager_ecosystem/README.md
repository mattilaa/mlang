# Package Manager Ecosystem

This demo creates an RSA key pair, packages and signs an MLang command with
SHA-256,
publishes it to a local protocol-v1 registry, installs and runs the verified
package, writes a CycloneDX SBOM, and evaluates an advisory database with two
severity policies.

Requirements: `openssl`, `tar`, `curl`, and CMake's command-line utility.

Run from the repository root:

```sh
./examples/package_manager_ecosystem/run_demo.sh
```

All generated keys, registry data, caches, installed files, and reports stay
under this example's ignored `build/` directory.
