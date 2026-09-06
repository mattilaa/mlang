#!/bin/sh
set -eu

cd "$(dirname "$0")"
compiler="${MLANG:-../../build/mlang}"

cmake -E remove_directory build
cmake -E make_directory build/keys
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 \
    -out build/keys/private.pem
openssl pkey -in build/keys/private.pem -pubout -out build/keys/public.pem

"$compiler" pkg package --sign-key build/keys/private.pem
"$compiler" pkg verify-signature \
    build/package/ecosystem_hello-1.2.0.tar.gz \
    --key build/keys/public.pem
"$compiler" pkg publish --sign-key build/keys/private.pem
"$compiler" pkg install 'ecosystem_hello@^1.0' \
    --root build/install --cache-dir build/cache --require-signature
./build/install/bin/ecosystem_hello

"$compiler" pkg sbom --output build/ecosystem.cdx.json
"$compiler" pkg audit --database advisories.toml --deny high \
    --cache-dir build/cache

if "$compiler" pkg audit --database advisories.toml --deny low \
    --cache-dir build/cache; then
    echo "expected low-severity audit policy to fail" >&2
    exit 1
fi

test -f build/ecosystem.cdx.json
test -f build/registry/index/ecosystem_hello.toml
test -f build/install/share/mlang/packages/ecosystem_hello-1.2.0.json
