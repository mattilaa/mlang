#!/bin/sh
set -eu

cd "$(dirname "$0")"
compiler="${MLANG:-../../build/mlang}"
cache_dir="$PWD/.demo-cache"
vendor_dir="$PWD/vendor"

"$compiler" pkg lock
"$compiler" pkg run show-config -p ergonomic_app --profile dev \
    --features telemetry --cache-dir "$cache_dir" --locked
"$compiler" pkg build -p ergonomic_app --profile dev \
    --features telemetry --cache-dir "$cache_dir" --locked
./apps/ergonomic_app/build/dev/ergonomic_app

"$compiler" pkg clean -p ergonomic_app --profile dev
"$compiler" pkg clean --config packages/telemetry/mlang.toml --profile dev
"$compiler" pkg build -p ergonomic_app --profile dev \
    --features telemetry --cache-dir "$cache_dir" --locked
./apps/ergonomic_app/build/dev/ergonomic_app

"$compiler" pkg vendor "$vendor_dir" -p ergonomic_app \
    --cache-dir "$cache_dir"
"$compiler" pkg build -p ergonomic_app --profile release \
    --features telemetry --vendor-dir "$vendor_dir" \
    --cache-dir "$cache_dir" --locked --offline
./apps/ergonomic_app/build/release/ergonomic_app

"$compiler" pkg build -p utility_app --release --cache-dir "$cache_dir"
./apps/utility_app/build/release/utility_app

