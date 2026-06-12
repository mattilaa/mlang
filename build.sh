#!/usr/bin/env sh
set -eu

source_dir="."
build_dir="build"
config_file=""
target=""
jobs=""
install=""
prefix=""
bin_dir=""

usage() {
    cat <<EOF
Usage: ./build.sh [options]

Options:
  -S, --source-dir DIR    Source directory (default: .)
  -B, --build-dir DIR     CMake build directory (default: build)
      --config-file FILE  mlang-config.conf to import (default: BUILD_DIR/mlang-config.conf)
  -j, --jobs N            Override saved parallel build jobs
      --target NAME       Build a specific CMake target only
  -i, --install           Install after build
      --no-install        Do not install after build
      --prefix DIR        Install prefix override
      --bin-dir DIR       Tool binary install dir override
  -h, --help              Show this help
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -S|--source-dir)
            source_dir="$2"
            shift 2
            ;;
        --source-dir=*)
            source_dir="${1#--source-dir=}"
            shift
            ;;
        -B|--build-dir)
            build_dir="$2"
            shift 2
            ;;
        --build-dir=*)
            build_dir="${1#--build-dir=}"
            shift
            ;;
        --config-file)
            config_file="$2"
            shift 2
            ;;
        --config-file=*)
            config_file="${1#--config-file=}"
            shift
            ;;
        -j|--jobs)
            jobs="$2"
            shift 2
            ;;
        --jobs=*)
            jobs="${1#--jobs=}"
            shift
            ;;
        --target)
            target="$2"
            shift 2
            ;;
        --target=*)
            target="${1#--target=}"
            shift
            ;;
        -i|--install)
            install="true"
            shift
            ;;
        --no-install)
            install="false"
            shift
            ;;
        --prefix)
            prefix="$2"
            shift 2
            ;;
        --prefix=*)
            prefix="${1#--prefix=}"
            shift
            ;;
        --bin-dir)
            bin_dir="$2"
            shift 2
            ;;
        --bin-dir=*)
            bin_dir="${1#--bin-dir=}"
            shift
            ;;
        *)
            echo "build.sh: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

config_value() {
    key="$1"
    file="$2"
    if [ ! -f "$file" ]; then
        return 0
    fi
    sed -n "s/^$key=//p" "$file" | tail -n 1
}

if [ -z "$config_file" ]; then
    config_file="$build_dir/mlang-config.conf"
fi

if [ ! -f "$config_file" ]; then
    echo "build.sh: missing config file: $config_file" >&2
    echo "Run ./bootstrap.sh and ./build/mlang-config first, or pass --config-file." >&2
    exit 1
fi

mlang_config="$build_dir/mlang-config"
if [ ! -x "$mlang_config" ]; then
    if [ -x "$build_dir/mlang-config.exe" ]; then
        mlang_config="$build_dir/mlang-config.exe"
    else
        echo "build.sh: $mlang_config is missing; bootstrapping first"
        ./bootstrap.sh --build-dir "$build_dir"
    fi
fi

cache_file="$build_dir/mlang_config_cache.cmake"
if [ -z "$jobs" ]; then
    jobs="$(config_value jobs "$config_file")"
fi
if [ -z "$prefix" ]; then
    prefix="$(config_value install_prefix "$config_file")"
fi
if [ -z "$prefix" ]; then
    prefix="$HOME/.local"
fi
if [ -z "$bin_dir" ]; then
    bin_dir="$(config_value bin_dir "$config_file")"
fi
if [ -z "$bin_dir" ]; then
    bin_dir="$prefix/bin"
fi

echo "$mlang_config --import $config_file --build-dir $build_dir --install-prefix $prefix --bin-dir $bin_dir --write"
"$mlang_config" --import "$config_file" --build-dir "$build_dir" --install-prefix "$prefix" --bin-dir "$bin_dir" --write

use_ninja=""
if command -v ninja >/dev/null 2>&1; then
    use_ninja="1"
fi

if [ -n "$use_ninja" ]; then
    echo "cmake -C $cache_file -S $source_dir -B $build_dir -G Ninja"
    cmake -C "$cache_file" -S "$source_dir" -B "$build_dir" -G Ninja
else
    echo "cmake -C $cache_file -S $source_dir -B $build_dir"
    cmake -C "$cache_file" -S "$source_dir" -B "$build_dir"
fi

if [ -n "$target" ]; then
    if [ -n "$jobs" ]; then
        echo "cmake --build $build_dir --config Release --target $target --parallel $jobs"
        cmake --build "$build_dir" --config Release --target "$target" --parallel "$jobs"
    else
        echo "cmake --build $build_dir --config Release --target $target"
        cmake --build "$build_dir" --config Release --target "$target"
    fi
    exit 0
fi

if [ -n "$jobs" ]; then
    echo "cmake --build $build_dir --config Release --target mlang mlang_std mlang-config --parallel $jobs"
    cmake --build "$build_dir" --config Release --target mlang mlang_std mlang-config --parallel "$jobs"
else
    echo "cmake --build $build_dir --config Release --target mlang mlang_std mlang-config"
    cmake --build "$build_dir" --config Release --target mlang mlang_std mlang-config
fi

mlang="$build_dir/mlang"
if [ ! -x "$mlang" ] && [ -x "$build_dir/mlang.exe" ]; then
    mlang="$build_dir/mlang.exe"
fi
if [ ! -x "$mlang" ] && [ -x "$build_dir/Release/mlang.exe" ]; then
    mlang="$build_dir/Release/mlang.exe"
fi

build_mla_tool() {
    src="$1"
    out="$2"
    if [ -d "$build_dir/Release" ]; then
        echo "$mlang $src --no-tests -Wno-unwrap -O0 -L $build_dir -L $build_dir/Release -lmlang_std -o $out"
        "$mlang" "$src" --no-tests -Wno-unwrap -O0 -L "$build_dir" -L "$build_dir/Release" -lmlang_std -o "$out"
    else
        echo "$mlang $src --no-tests -Wno-unwrap -O0 -L $build_dir -lmlang_std -o $out"
        "$mlang" "$src" --no-tests -Wno-unwrap -O0 -L "$build_dir" -lmlang_std -o "$out"
    fi
}

build_mla_tool "tools/mlangd-mla/main.mla" "$build_dir/mlangd-mla"
build_mla_tool "tools/mlang-format-mla/main.mla" "$build_dir/mlang-format"
build_mla_tool "tools/mlang-frontend-mla/main.mla" "$build_dir/mlang-frontend-mla"
build_mla_tool "tools/mlangpkg/mlangpkg.mla" "$build_dir/mlangpkg"

cat > "$build_dir/mlang-frontend" <<'EOF'
#!/usr/bin/env sh
set -eu
bin_dir="$(cd "$(dirname "$0")" && pwd)"
exec "$bin_dir/mlang-frontend-mla" --backend "$bin_dir/mlang" "$@"
EOF
chmod +x "$build_dir/mlang-frontend"

if [ "$install" = "true" ] || [ "$install" = "ON" ] || [ "$install" = "1" ]; then
    echo "cmake --install $build_dir --config Release --prefix $prefix"
    cmake --install "$build_dir" --config Release --prefix "$prefix"
    echo "installing MLang-built tools to $bin_dir"
    mkdir -p "$bin_dir"
    cp -f "$build_dir/mlangd-mla" "$bin_dir/mlangd-mla"
    cp -f "$build_dir/mlang-format" "$bin_dir/mlang-format"
    cp -f "$build_dir/mlang-frontend-mla" "$bin_dir/mlang-frontend-mla"
    cp -f "$build_dir/mlang-frontend" "$bin_dir/mlang-frontend"
    cp -f "$build_dir/mlangpkg" "$bin_dir/mlangpkg"
    chmod +x "$bin_dir/mlangd-mla" "$bin_dir/mlang-format" "$bin_dir/mlang-frontend-mla" "$bin_dir/mlang-frontend" "$bin_dir/mlangpkg"
fi
