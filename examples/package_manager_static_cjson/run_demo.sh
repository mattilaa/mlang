#!/usr/bin/env bash
set -euo pipefail

compiler="${MLANG_BIN:-../../build/mlang}"

"$compiler" pkg fetch
"$compiler" pkg build
./build/static_cjson_demo
"$compiler" pkg clean
