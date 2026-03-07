#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: build_install.sh [--install] [--no-install] [--prefix <path>] [--bin-dir <path>] [--system] [--sudo] [--build-dir <dir>] [--use-make] [--all] [--help]
                        [-j <n>] [--jobs <n>] [--robot-jobs <n>] [--time-format <full|hms|off>] [--robot-time-format <full|hms|off>]
                        [--artifacts-dir <dir>]
                        [--log-level <error|info|verbose|debug>] [--color-logs] [--no-color-logs]
                        [--unit-tests [<path>]] [--lsp-tests] [--robot-tests] [--tests [<path>]] [--all-tests] [--no-tests]
                        [--install-if-tests-pass]
                        [--merge-to-main] [--bump-minor] [--bump-major]

Builds the mlang compiler toolchain and optionally installs it.

Options:
  --install          Install after build (default)
  --no-install       Skip install step
  --prefix <path>    Install prefix (default: $HOME/.local)
  --bin-dir <path>   Binary install dir (default: <prefix>/bin, i.e. $HOME/.local/bin)
  --system           Install to /usr/local (implies --sudo)
  --sudo             Use sudo for install step
  --build-dir <dir>  Build directory (default: build)
  --use-make         Use Unix Makefiles instead of Ninja
  --all              Build/install mlang and mlang_std (default behavior)
  -j, --jobs <n>     Parallel jobs for robot tests (default: all CPU cores)
  --robot-jobs <n>   Same as --jobs, explicit robot-only naming
  --time-format <f>  Robot progress timestamp format: full, hms, off (default: full)
  --robot-time-format <f>  Same as --time-format, explicit robot-only naming
  --artifacts-dir <dir>  Store robot/unit results and test binaries (default: artifacts)
  --log-level <lvl>  Log verbosity: error, info, verbose, debug (default: info)
  --color-logs       Force colored log output
  --no-color-logs    Disable colored log output
  --tests [<path>]   Run tests after build (default: unit + lsp + robot). If <path>
                     is provided, run only that mlang test target.
  --unit-tests [<path>] Run unit tests after build. If <path> is provided,
                     run only that mlang test target.
  --lsp-tests        Run Python transcript/integration tests for mlangd-mla.
  --robot-tests      Run robot tests after build (installs only if tests pass)
  --all-tests        Run unit + lsp + robot tests.
  --no-tests         Skip all tests (default)
  --install-if-tests-pass  Install only if requested tests pass
  --merge-to-main    Merge current branch into main (fast-forward if possible,
                     otherwise creates a merge commit), then stay on main.
                     Happens before the build step.
  --bump-minor       Increment MLANG_VERSION_MINOR by 1, reset build counter
                     to 0, and commit the change. Happens after --merge-to-main
                     (if requested) and before the build step.
  --bump-major       Increment MLANG_VERSION_MAJOR by 1, reset MINOR and build
                     counter to 0, and commit the change. Happens after
                     --merge-to-main (if requested) and before the build step.
  --help             Show this help

Notes:
  The install step also installs the stdlib docs (including builtin types)
  to: <prefix>/share/mlang/stdlib
  and stdlib libraries to: <prefix>/lib
  and tool binaries: mlangd-mla, mlang-format, mlang-frontend-mla, mlang-frontend
  - Install runs by default unless --no-install is set.
  - --tests/--unit-tests/--lsp-tests/--robot-tests/--all-tests imply --install-if-tests-pass.
  - --install-if-tests-pass requires tests to be selected.
  - --no-tests and --no-install clear --install-if-tests-pass.
  - --bump-minor and --bump-major can be combined with --merge-to-main for a
    full release workflow: merge branch, bump version, build, test, install.
USAGE
}

install_after_build=true
prefix="${HOME}/.local"
bin_dir=""
use_sudo=false
build_dir="build"
generator="Ninja"
build_all=true
run_unit_tests=false
run_lsp_tests=false
run_robot_tests=false
install_if_tests_pass=false
test_target=""
merge_to_main=false
bump_minor=false
bump_major=false
robot_jobs=""
robot_time_format="full"
artifacts_dir="artifacts"
log_level="info"
color_logs="auto"

log_level_value() {
  case "$1" in
    error) echo 0 ;;
    info) echo 1 ;;
    verbose) echo 2 ;;
    debug) echo 3 ;;
    *) return 1 ;;
  esac
}

COLOR_RESET=""
COLOR_ERROR=""
COLOR_INFO=""
COLOR_PASS=""
COLOR_VERBOSE=""
COLOR_DEBUG=""
COLOR_ENABLED=false

init_log_colors() {
  local use_color=false
  local term_name="${TERM:-}"
  local term_colors=0
  if command -v tput >/dev/null 2>&1; then
    term_colors="$(tput colors 2>/dev/null || echo 0)"
  fi
  if [[ "$color_logs" == "always" ]]; then
    use_color=true
  elif [[ "$color_logs" == "never" ]]; then
    use_color=false
  elif [[ -t 1 && "$term_name" != "dumb" ]]; then
    if [[ "$term_colors" =~ ^[0-9]+$ ]] && (( term_colors >= 8 )); then
      use_color=true
    elif [[ -n "${COLORTERM:-}" ]]; then
      use_color=true
    fi
  fi

  if [[ -n "${NO_COLOR:-}" ]]; then
    use_color=false
  fi
  if [[ "${CLICOLOR_FORCE:-0}" != "0" ]]; then
    use_color=true
  elif [[ "${CLICOLOR:-}" == "0" ]]; then
    use_color=false
  fi

  COLOR_ENABLED=$use_color
  if $use_color; then
    COLOR_RESET=$'\033[0m'
    COLOR_ERROR=$'\033[31m'
    COLOR_INFO=$'\033[34m'
    COLOR_PASS=$'\033[32m'
    COLOR_VERBOSE=$'\033[36m'
    COLOR_DEBUG=$'\033[90m'
  fi
}

log_emit() {
  local level="$1"
  local color="$2"
  local text="$3"
  local want cur
  want=$(log_level_value "$level")
  cur=$(log_level_value "$log_level")
  if (( want <= cur )); then
    local level_upper
    level_upper="$(printf '%s' "$level" | tr '[:lower:]' '[:upper:]')"
    local prefix="[${level_upper}]"
    if [[ -n "$color" ]]; then
      if [[ "$level" == "error" ]]; then
        printf "%b%s%b %s\n" "$color" "$prefix" "$COLOR_RESET" "$text" >&2
      else
        printf "%b%s%b %s\n" "$color" "$prefix" "$COLOR_RESET" "$text"
      fi
    else
      if [[ "$level" == "error" ]]; then
        printf "%s %s\n" "$prefix" "$text" >&2
      else
        printf "%s %s\n" "$prefix" "$text"
      fi
    fi
  fi
}

log_error() { log_emit "error" "$COLOR_ERROR" "$*"; }
log_info() { log_emit "info" "$COLOR_INFO" "$*"; }
log_verbose() { log_emit "verbose" "$COLOR_VERBOSE" "$*"; }
log_debug() { log_emit "debug" "$COLOR_DEBUG" "$*"; }

FAILED_CASES_TMP="$(mktemp)"

cleanup_and_report_failed_cases() {
  local rc="${1:-0}"
  if [[ -n "${FAILED_CASES_TMP:-}" && -f "$FAILED_CASES_TMP" ]]; then
    if [[ -s "$FAILED_CASES_TMP" ]]; then
      echo ""
      log_error "FAILED TEST CASE SUMMARY:"
      while IFS= read -r tc; do
        log_error "  - $tc"
      done < "$FAILED_CASES_TMP"
    fi
    rm -f "$FAILED_CASES_TMP"
  fi
  return "$rc"
}

trap 'cleanup_and_report_failed_cases "$?"' EXIT

report_failed_tests() {
  local label="$1"
  local tmp_log="$2"
  local failed_tmp
  local found=1
  failed_tmp="$(mktemp)"
  awk '
    function strip_ansi(s) {
      gsub(/\033\[[0-9;]*[A-Za-z]/, "", s)
      return s
    }
    {
      line = strip_ansi($0)
      sub(/^[0-9][0-9]\/[0-9][0-9]\/[0-9][0-9][0-9][0-9]: /, "", line)
      if (line ~ /^\[SUITE\] /) {
        cur_suite = line
        sub(/^\[SUITE\] /, "", cur_suite)
        next
      }
      if (line ~ /^\[  FAILED  \] /) {
        sub(/^\[  FAILED  \] /, "", line)
        print line
        next
      }
      if (line ~ /^\[FAIL\] /) {
        sub(/^\[FAIL\] /, "", line)
        sub(/ \(rc=-?[0-9]+\)$/, "", line)
        print line
        next
      }
      if (line ~ /^\[SUITE FAIL\] /) {
        sub(/^\[SUITE FAIL\] /, "", line)
        print line
        next
      }
      if (line ~ /^\[SUMMARY\] / && line ~ /fail=[1-9][0-9]*/) {
        summary = line
        if (cur_suite != "") {
          print cur_suite " " summary
        } else {
          print summary
        }
        next
      }
    }
  ' "$tmp_log" | sed '/^[[:space:]]*$/d' | sort -u > "$failed_tmp"

  if [[ -s "$failed_tmp" ]]; then
    found=0
    log_error "$label failed test cases:"
    while IFS= read -r tc; do
      log_error "  - $tc"
      printf "%s: %s\n" "$label" "$tc" >> "$FAILED_CASES_TMP"
    done < "$failed_tmp"
    sort -u "$FAILED_CASES_TMP" -o "$FAILED_CASES_TMP"
  fi
  rm -f "$failed_tmp"
  return "$found"
}

run_checked_command() {
  local label="$1"
  shift
  local tmp_log
  tmp_log="$(mktemp)"
  set +e
  if $COLOR_ENABLED; then
    "$@" 2>&1 | tee "$tmp_log" | awk \
      -v creset="$COLOR_RESET" \
      -v cfail="$COLOR_ERROR" \
      -v cpass="$COLOR_PASS" '
      {
        line = $0
        gsub(/\[  FAILED  \]/, "[" cfail "  FAILED  " creset "]", line)
        gsub(/\[  PASSED  \]/, "[" cpass "  PASSED  " creset "]", line)
        gsub(/\[       OK \]/, "[" cpass "       OK " creset "]", line)
        gsub(/\[PASS\]/, "[" cpass "PASS" creset "]", line)
        gsub(/\[FAIL\]/, "[" cfail "FAIL" creset "]", line)
        gsub(/\[SUITE PASS\]/, "[" cpass "SUITE PASS" creset "]", line)
        gsub(/\[SUITE FAIL\]/, "[" cfail "SUITE FAIL" creset "]", line)
        print line
        fflush()
      }
    '
    local cmd_rc=${PIPESTATUS[0]}
  else
    "$@" 2>&1 | tee "$tmp_log"
    local cmd_rc=${PIPESTATUS[0]}
  fi
  set -e

  if [[ $cmd_rc -ne 0 ]]; then
    log_error "$label failed with exit code $cmd_rc"
    report_failed_tests "$label" "$tmp_log" || true
    rm -f "$tmp_log"
    return "$cmd_rc"
  fi

  if report_failed_tests "$label" "$tmp_log"; then
    log_error "$label reported test failures in output"
    rm -f "$tmp_log"
    return 1
  fi

  rm -f "$tmp_log"
  return 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    --install)
      install_after_build=true
      shift
      ;;
    --no-install)
      install_after_build=false
      install_if_tests_pass=false
      shift
      ;;
    --prefix)
      if [[ $# -lt 2 ]]; then
        echo "error: --prefix requires a value" >&2
        exit 1
      fi
      prefix="$2"
      shift 2
      ;;
    --bin-dir)
      if [[ $# -lt 2 ]]; then
        echo "error: --bin-dir requires a value" >&2
        exit 1
      fi
      bin_dir="$2"
      shift 2
      ;;
    --system)
      prefix="/usr/local"
      use_sudo=true
      shift
      ;;
    --sudo)
      use_sudo=true
      shift
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "error: --build-dir requires a value" >&2
        exit 1
      fi
      build_dir="$2"
      shift 2
      ;;
    --use-make)
      generator="Unix Makefiles"
      shift
      ;;
    --all)
      build_all=true
      shift
      ;;
    -j|--jobs|--robot-jobs)
      if [[ $# -lt 2 ]]; then
        echo "error: $1 requires a value" >&2
        exit 1
      fi
      robot_jobs="$2"
      shift 2
      ;;
    --time-format|--robot-time-format)
      if [[ $# -lt 2 ]]; then
        echo "error: $1 requires a value" >&2
        exit 1
      fi
      robot_time_format="$2"
      shift 2
      ;;
    --time-format=*|--robot-time-format=*)
      robot_time_format="${1#*=}"
      shift
      ;;
    --artifacts-dir)
      if [[ $# -lt 2 ]]; then
        echo "error: --artifacts-dir requires a value" >&2
        exit 1
      fi
      artifacts_dir="$2"
      shift 2
      ;;
    --artifacts-dir=*)
      artifacts_dir="${1#*=}"
      shift
      ;;
    --jobs=*|--robot-jobs=*)
      robot_jobs="${1#*=}"
      shift
      ;;
    -j*)
      robot_jobs="${1#-j}"
      shift
      ;;
    --log-level)
      if [[ $# -lt 2 ]]; then
        echo "error: --log-level requires a value" >&2
        exit 1
      fi
      if ! log_level_value "$2" >/dev/null; then
        echo "error: invalid --log-level '$2' (use: error|info|verbose|debug)" >&2
        exit 1
      fi
      log_level="$2"
      shift 2
      ;;
    --color-logs)
      color_logs="always"
      shift
      ;;
    --no-color-logs)
      color_logs="never"
      shift
      ;;
    --tests)
      run_unit_tests=true
      run_lsp_tests=true
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      if [[ $# -gt 0 && "$1" != --* ]]; then
        test_target="$1"
        run_lsp_tests=false
        run_robot_tests=false
        shift
      fi
      ;;
    --unit-tests)
      run_unit_tests=true
      install_if_tests_pass=true
      shift
      if [[ $# -gt 0 && "$1" != --* ]]; then
        test_target="$1"
        shift
      fi
      ;;
    --lsp-tests)
      run_lsp_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --robot-tests)
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --all-tests)
      run_unit_tests=true
      run_lsp_tests=true
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --no-tests)
      run_unit_tests=false
      run_lsp_tests=false
      run_robot_tests=false
      install_if_tests_pass=false
      shift
      ;;
    --install-if-tests-pass)
      install_if_tests_pass=true
      shift
      ;;
    --merge-to-main)
      merge_to_main=true
      shift
      ;;
    --bump-minor)
      bump_minor=true
      shift
      ;;
    --bump-major)
      bump_major=true
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$robot_time_format" != "full" && "$robot_time_format" != "hms" && "$robot_time_format" != "off" ]]; then
  echo "error: invalid time format '$robot_time_format' (use: full|hms|off)" >&2
  exit 1
fi
mkdir -p "$artifacts_dir/cpp" "$artifacts_dir/unit" "$artifacts_dir/robot"

init_log_colors
log_debug "shell=${SHELL:-<unset>} bash_version=${BASH_VERSION:-<unset>} term=${TERM:-<unset>} colorterm=${COLORTERM:-<unset>} color_mode=${color_logs} color_enabled=${COLOR_ENABLED}"

if [[ -z "$bin_dir" ]]; then
  bin_dir="${prefix}/bin"
fi

# ============================================================================
# Version bump helper
# ============================================================================
bump_version() {
  local mode="$1"   # "major" or "minor"
  local cmake_file="CMakeLists.txt"

  local cur_major cur_minor
  cur_major=$(grep 'set(MLANG_VERSION_MAJOR' "$cmake_file" | grep -o '[0-9]*')
  cur_minor=$(grep 'set(MLANG_VERSION_MINOR' "$cmake_file" | grep -o '[0-9]*')

  local new_major="$cur_major"
  local new_minor="$cur_minor"

  if [[ "$mode" == "major" ]]; then
    new_major=$(( cur_major + 1 ))
    new_minor=0
  else
    new_minor=$(( cur_minor + 1 ))
  fi

  log_info "version: ${cur_major}.${cur_minor} -> ${new_major}.${new_minor}"

  # Update CMakeLists.txt (macOS-safe sed with empty backup extension)
  sed -i '' "s/set(MLANG_VERSION_MAJOR ${cur_major})/set(MLANG_VERSION_MAJOR ${new_major})/" "$cmake_file"
  sed -i '' "s/set(MLANG_VERSION_MINOR ${cur_minor})/set(MLANG_VERSION_MINOR ${new_minor})/" "$cmake_file"

  # Reset the persisted build counter so the next build starts from 1
  if [[ -f "${build_dir}/.mlang_build_number" ]]; then
    echo "0" > "${build_dir}/.mlang_build_number"
  fi

  git add "$cmake_file"
  git commit -m "Update version: mlang ${new_major}.${new_minor}"
  log_info "committed version bump: v${new_major}.${new_minor}"
}

# ============================================================================
# Step 1: merge to main (if requested)
# ============================================================================
log_info "step 1/3: merge checks"
if $merge_to_main; then
  source_branch=$(git rev-parse --abbrev-ref HEAD)
  if [[ "$source_branch" == "main" ]]; then
    log_error "already on main — nothing to merge"
    exit 1
  fi
  if ! git diff --quiet || ! git diff --cached --quiet; then
    log_error "working tree has uncommitted changes; commit or stash before merging"
    exit 1
  fi
  log_info "merging '$source_branch' into main..."
  git checkout main
  git merge "$source_branch" --no-ff -m "Merge branch '${source_branch}' into main"
  log_info "merged '$source_branch' into main"
fi

# ============================================================================
# Step 2: version bump (if requested)
# ============================================================================
log_info "step 2/3: version bump checks"
if $bump_major; then
  bump_version "major"
elif $bump_minor; then
  bump_version "minor"
fi

# ============================================================================
# Step 3: build
# ============================================================================
log_info "step 3/3: build and optional tests/install"
cmake -S . -B "$build_dir" -G "$generator" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
if $build_all; then
  log_info "building targets: mlang mlang_std"
  cmake --build "$build_dir" --target mlang mlang_std
fi

# Build mlangd-mla with the freshly built compiler/runtime.
log_info "building tool: mlangd-mla"
"$build_dir/mlang" "tools/mlangd-mla/main.mla" -L "$build_dir" -lmlang_std -o "$build_dir/mlangd-mla"
# Build mlang-format (Mlang implementation, port-in-progress).
log_info "building tool: mlang-format"
"$build_dir/mlang" "tools/mlang-format-mla/main.mla" -L "$build_dir" -lmlang_std -o "$build_dir/mlang-format"
# Build mlang-frontend-mla (feature-rich Mlang CLI frontend).
log_info "building tool: mlang-frontend-mla"
"$build_dir/mlang" "tools/mlang-frontend-mla/main.mla" -L "$build_dir" -lmlang_std -o "$build_dir/mlang-frontend-mla"

if $run_unit_tests; then
  log_info "running unit tests"
  if [[ -n "$test_target" ]]; then
    if [[ "$color_logs" == "never" ]]; then
      run_checked_command "mlang tests ($test_target)" env NO_COLOR=1 CLICOLOR=0 MLANG_ARTIFACT_DIR="$artifacts_dir/cpp" PATH=".:${PATH}" "$build_dir/mlang" test "$test_target"
    else
      run_checked_command "mlang tests ($test_target)" env MLANG_ARTIFACT_DIR="$artifacts_dir/cpp" PATH=".:${PATH}" "$build_dir/mlang" test "$test_target"
    fi
  else
    # C++/ctest suite
    if [[ "$color_logs" == "never" ]]; then
      run_checked_command "ctest suite" env NO_COLOR=1 CLICOLOR=0 CTEST_COLOR=0 GTEST_COLOR=no TEST_BUILD_DIR="$artifacts_dir/unit/ctest" ./tests/run_tests.sh --output-on-failure
    else
      run_checked_command "ctest suite" env TEST_BUILD_DIR="$artifacts_dir/unit/ctest" ./tests/run_tests.sh --output-on-failure
    fi
    # MLang test suite (*.mla under tests/)
    if [[ "$color_logs" == "never" ]]; then
      run_checked_command "mlang tests (tests)" env NO_COLOR=1 CLICOLOR=0 MLANG_ARTIFACT_DIR="$artifacts_dir/cpp" PATH=".:${PATH}" "$build_dir/mlang" test tests
    else
      run_checked_command "mlang tests (tests)" env MLANG_ARTIFACT_DIR="$artifacts_dir/cpp" PATH=".:${PATH}" "$build_dir/mlang" test tests
    fi
  fi
fi

if $run_lsp_tests; then
  log_info "running lsp tests"
  if [[ "$color_logs" == "never" ]]; then
    run_checked_command "mlang-format e2e tests" env NO_COLOR=1 CLICOLOR=0 python3 tests/mlang_format_spacing_e2e.py --mlang-format "$build_dir/mlang-format"
    run_checked_command "mlangd transcript tests" env NO_COLOR=1 CLICOLOR=0 python3 tests/lsp_mlangd-mla_transcripts.py --mlangd "$build_dir/mlangd-mla"
  else
    run_checked_command "mlang-format e2e tests" python3 tests/mlang_format_spacing_e2e.py --mlang-format "$build_dir/mlang-format"
    run_checked_command "mlangd transcript tests" python3 tests/lsp_mlangd-mla_transcripts.py --mlangd "$build_dir/mlangd-mla"
  fi
fi

if $run_robot_tests; then
  log_info "running robot tests"
  if [[ "$color_logs" == "never" ]]; then
    run_checked_command "robot tests" env \
      ROBOT_LOG_LEVEL="$log_level" \
      ROBOT_COLOR_LOGS="$color_logs" \
      ROBOT_JOBS="$robot_jobs" \
      ROBOT_TIME_FORMAT="$robot_time_format" \
      ROBOT_RESULTS_DIR="$artifacts_dir/robot" \
      MLANG_ARTIFACT_DIR="$artifacts_dir/cpp" \
      ROBOT_CONSOLE_COLORS=off \
      PYTHONUNBUFFERED=1 \
      NO_COLOR=1 \
      CLICOLOR=0 \
      ./tests/run_examples_robot.sh
  else
    run_checked_command "robot tests" env \
      ROBOT_LOG_LEVEL="$log_level" \
      ROBOT_COLOR_LOGS="$color_logs" \
      ROBOT_JOBS="$robot_jobs" \
      ROBOT_TIME_FORMAT="$robot_time_format" \
      ROBOT_RESULTS_DIR="$artifacts_dir/robot" \
      MLANG_ARTIFACT_DIR="$artifacts_dir/cpp" \
      ROBOT_CONSOLE_COLORS=ansi \
      PYTHONUNBUFFERED=1 \
      ./tests/run_examples_robot.sh
  fi
fi

if $install_after_build; then
  log_info "install phase started"
  if $install_if_tests_pass && ! $run_unit_tests && ! $run_lsp_tests && ! $run_robot_tests; then
    log_error "--install-if-tests-pass requires --tests, --unit-tests, --lsp-tests, --robot-tests, or --all-tests"
    exit 1
  fi
  if $use_sudo; then
    sudo cmake --install "$build_dir" --prefix "$prefix"
  else
    cmake --install "$build_dir" --prefix "$prefix"
  fi

  # Install tool binaries explicitly to the selected bin dir.
  frontend_launcher_tmp="$(mktemp)"
  cat > "$frontend_launcher_tmp" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
bin_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${bin_dir}/mlang-frontend-mla" --backend "${bin_dir}/mlang" "$@"
EOF
  if $use_sudo; then
    sudo mkdir -p "$bin_dir"
    sudo cp -f "$build_dir/mlangd-mla" "$bin_dir/mlangd-mla"
    sudo chmod +x "$bin_dir/mlangd-mla"
    sudo cp -f "$build_dir/mlang-format" "$bin_dir/mlang-format"
    sudo chmod +x "$bin_dir/mlang-format"
    sudo cp -f "$build_dir/mlang-frontend-mla" "$bin_dir/mlang-frontend-mla"
    sudo chmod +x "$bin_dir/mlang-frontend-mla"
    sudo cp -f "$frontend_launcher_tmp" "$bin_dir/mlang-frontend"
    sudo chmod +x "$bin_dir/mlang-frontend"
  else
    mkdir -p "$bin_dir"
    cp -f "$build_dir/mlangd-mla" "$bin_dir/mlangd-mla"
    chmod +x "$bin_dir/mlangd-mla"
    cp -f "$build_dir/mlang-format" "$bin_dir/mlang-format"
    chmod +x "$bin_dir/mlang-format"
    cp -f "$build_dir/mlang-frontend-mla" "$bin_dir/mlang-frontend-mla"
    chmod +x "$bin_dir/mlang-frontend-mla"
    cp -f "$frontend_launcher_tmp" "$bin_dir/mlang-frontend"
    chmod +x "$bin_dir/mlang-frontend"
  fi
  rm -f "$frontend_launcher_tmp"
  log_info "installed mlangd-mla: $bin_dir/mlangd-mla"
  log_info "installed mlang-format: $bin_dir/mlang-format"
  log_info "installed mlang-frontend-mla: $bin_dir/mlang-frontend-mla"
  log_info "installed mlang-frontend: $bin_dir/mlang-frontend"

  stdlib_dir="$prefix/share/mlang/stdlib"
  stdlib_lib_dir="$prefix/lib"
  stdlib_lib_src_dir="$prefix/lib/mlang"
  if [[ -f "$stdlib_dir/types.mla" ]]; then
    log_info "installed stdlib docs: $stdlib_dir"
  else
    log_error "stdlib docs not found at $stdlib_dir"
    log_error "ensure stdlib/ is present in the repo and install again"
  fi
  # Ensure stdlib lib lives in <prefix>/lib
  if [[ -d "$stdlib_lib_src_dir" ]]; then
    if [[ -f "$stdlib_lib_src_dir/libmlang_std.a" ]]; then
      cp -f "$stdlib_lib_src_dir/libmlang_std.a" "$stdlib_lib_dir/"
    fi
    if [[ -f "$stdlib_lib_src_dir/libmlang_std.dylib" ]]; then
      cp -f "$stdlib_lib_src_dir/libmlang_std.dylib" "$stdlib_lib_dir/"
    fi
    if [[ -f "$stdlib_lib_src_dir/libmlang_std.so" ]]; then
      cp -f "$stdlib_lib_src_dir/libmlang_std.so" "$stdlib_lib_dir/"
    fi
  fi

  if [[ -f "$stdlib_lib_dir/libmlang_std.a" || -f "$stdlib_lib_dir/libmlang_std.dylib" || -f "$stdlib_lib_dir/libmlang_std.so" ]]; then
    log_info "installed stdlib lib: $stdlib_lib_dir"
  else
    log_error "stdlib lib not found at $stdlib_lib_dir"
    log_error "ensure stdlib/src is present and install again"
  fi
else
  log_verbose "stdlib docs (builtin type definitions) are not installed"
  log_verbose "run with --install to copy stdlib to <prefix>/share/mlang/stdlib"
fi
