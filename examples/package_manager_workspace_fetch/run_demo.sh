#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")" && pwd)"
compiler="$root_dir/../../build/mlang"

cd "$root_dir"

"$compiler" pkg fetch
"$compiler" pkg build

echo
echo "[git package]"
./packages/git_cjson_demo/build/git_cjson_demo

echo
echo "[tar.gz package]"
./packages/tarball_cjson_demo/build/tarball_cjson_demo

echo
"$compiler" pkg clean
