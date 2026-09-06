#!/bin/sh
set -eu

cd "$(dirname "$0")"
compiler="${MLANG:-../../build/mlang}"

"$compiler" pkg tree
"$compiler" pkg why math
"$compiler" pkg lock
"$compiler" pkg build --locked
"$compiler" pkg verify
./build/path_dependency_app
