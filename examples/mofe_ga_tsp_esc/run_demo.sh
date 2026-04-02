#!/bin/sh
set -eu
BIN="/tmp/mofe_ga_tsp_esc"
./build/mlang examples/mofe_ga_tsp_esc/main.mla -L build -lmlang_std -o "${BIN}"
exec "${BIN}" "$@"
