#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")" && pwd)"
compiler="$root_dir/../../build/mlang"

cd "$root_dir"
"$compiler" pkg build

test -x build/editor/editor
test -x build/editor/asset-compiler
test -x build/converter/converter

./build/editor/editor
./build/editor/asset-compiler
./build/converter/converter
