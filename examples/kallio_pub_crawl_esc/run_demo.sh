#!/bin/sh
set -eu
BIN="/tmp/kallio_pub_crawl_esc"
./build/mlang examples/kallio_pub_crawl_esc/main.mla -L build -lmlang_std -o "${BIN}"
exec "${BIN}" "$@"
