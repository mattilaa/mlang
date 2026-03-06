#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MLANG_BIN="${MLANG_BIN:-$ROOT_DIR/build/mlang}"
ROBOT_RESULTS_DIR="${ROBOT_RESULTS_DIR:-$ROOT_DIR/artifacts/robot}"
LOG_LEVEL="${ROBOT_LOG_LEVEL:-info}"
COLOR_LOGS="${ROBOT_COLOR_LOGS:-auto}"
ROBOT_JOBS="${ROBOT_JOBS:-}"
ROBOT_TIME_FORMAT="${ROBOT_TIME_FORMAT:-full}"
ROBOT_ALLOW_FLAKY_PASS="${ROBOT_ALLOW_FLAKY_PASS:-0}"
ROBOT_SHOW_RUN="${ROBOT_SHOW_RUN:-0}"
PROGRESS_LISTENER="$ROOT_DIR/tests/robot_progress_listener.py"
PABOT_FILTER="$ROOT_DIR/tests/pabot_progress_filter.py"
PABOT_ROBOT_WRAPPER="$ROOT_DIR/tests/pabot_robot_wrapper.py"
PARALLEL_RUNNER="$ROOT_DIR/tests/robot_parallel_runner.py"

if [[ "$ROBOT_RESULTS_DIR" != /* ]]; then
  ROBOT_RESULTS_DIR="$ROOT_DIR/$ROBOT_RESULTS_DIR"
fi

usage() {
  cat <<'USAGE'
Usage: run_examples_robot.sh [-j <n>] [--jobs <n>] [--time-format <full|hms|off>] [--show-run] [--log-level <error|info|verbose|debug>] [--color-logs] [--no-color-logs] [--help]

Options:
  -j, --jobs <n>     Number of parallel jobs for robot tests (default: CPU cores)
  --time-format <f>  Timestamp format in progress lines: full, hms, off (default: full)
  --show-run         Show [RUN ] progress rows (default: hidden)
  --log-level <lvl>  Log verbosity: error, info, verbose, debug (default: info)
  --color-logs       Force colored log output
  --no-color-logs    Disable colored log output
  --help             Show this help
USAGE
}

detect_jobs() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
    return
  fi
  if command -v sysctl >/dev/null 2>&1; then
    sysctl -n hw.ncpu
    return
  fi
  if command -v getconf >/dev/null 2>&1; then
    getconf _NPROCESSORS_ONLN
    return
  fi
  echo 1
}

detect_robot_total_tests() {
  local tmp_xml rc total
  tmp_xml="$(mktemp)"
  set +e
  "${ROBOT_CMD[@]}" \
    --dryrun \
    --console none \
    --log none \
    --report none \
    --output "$tmp_xml" \
    "$ROOT_DIR/tests/robot/examples.robot" >/dev/null 2>&1
  rc=$?
  set -e
  if [[ $rc -ne 0 || ! -s "$tmp_xml" ]]; then
    rm -f "$tmp_xml"
    echo 0
    return
  fi
  total="$("$PYTHON_BIN" - "$tmp_xml" <<'PY'
import sys
import xml.etree.ElementTree as ET
path = sys.argv[1]
try:
    root = ET.parse(path).getroot()
    print(len(root.findall(".//test")))
except Exception:
    print(0)
PY
)"
  rm -f "$tmp_xml"
  if [[ -z "$total" ]]; then
    total=0
  fi
  echo "$total"
}

print_robot_failure_summary() {
  local xml_path="$1"
  if [[ ! -f "$xml_path" ]]; then
    return
  fi
  "$PYTHON_BIN" - "$xml_path" <<'PY'
import sys
import xml.etree.ElementTree as ET

path = sys.argv[1]
try:
    root = ET.parse(path).getroot()
except Exception:
    sys.exit(0)

failed = []
for t in root.findall(".//test"):
    st = t.find("status")
    if st is None:
        continue
    if (st.get("status") or "").upper() == "FAIL":
        name = t.get("name", "<unnamed test>")
        msg = (st.text or "").strip().replace("\n", " ")
        if len(msg) > 200:
            msg = msg[:197] + "..."
        failed.append((name, msg))

if not failed:
    sys.exit(0)

print("")
print(f"ROBOT FAIL SUMMARY ({len(failed)}):")
for name, msg in failed:
    if msg:
        print(f"- {name}: {msg}")
    else:
        print(f"- {name}")
PY
}

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
  if [[ "$COLOR_LOGS" == "always" ]]; then
    use_color=true
  elif [[ "$COLOR_LOGS" == "never" ]]; then
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
  cur=$(log_level_value "$LOG_LEVEL")
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    -j|--jobs)
      if [[ $# -lt 2 ]]; then
        echo "error: $1 requires a value" >&2
        exit 1
      fi
      ROBOT_JOBS="$2"
      shift 2
      ;;
    --jobs=*)
      ROBOT_JOBS="${1#*=}"
      shift
      ;;
    -j*)
      ROBOT_JOBS="${1#-j}"
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
      LOG_LEVEL="$2"
      shift 2
      ;;
    --time-format)
      if [[ $# -lt 2 ]]; then
        echo "error: --time-format requires a value" >&2
        exit 1
      fi
      ROBOT_TIME_FORMAT="$2"
      shift 2
      ;;
    --time-format=*)
      ROBOT_TIME_FORMAT="${1#*=}"
      shift
      ;;
    --show-run)
      ROBOT_SHOW_RUN=1
      shift
      ;;
    --color-logs)
      COLOR_LOGS="always"
      shift
      ;;
    --no-color-logs)
      COLOR_LOGS="never"
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$ROBOT_TIME_FORMAT" != "full" && "$ROBOT_TIME_FORMAT" != "hms" && "$ROBOT_TIME_FORMAT" != "off" ]]; then
  echo "error: invalid --time-format '$ROBOT_TIME_FORMAT' (use: full|hms|off)" >&2
  exit 1
fi

if [[ -z "$ROBOT_JOBS" ]]; then
  ROBOT_JOBS="$(detect_jobs)"
fi
if ! [[ "$ROBOT_JOBS" =~ ^[0-9]+$ ]] || [[ "$ROBOT_JOBS" -lt 1 ]]; then
  echo "error: invalid jobs value '$ROBOT_JOBS'" >&2
  exit 1
fi

init_log_colors
log_debug "shell=${SHELL:-<unset>} bash_version=${BASH_VERSION:-<unset>} term=${TERM:-<unset>} colorterm=${COLORTERM:-<unset>} color_mode=${COLOR_LOGS} color_enabled=${COLOR_ENABLED} jobs=${ROBOT_JOBS} time_format=${ROBOT_TIME_FORMAT}"

if [[ ! -x "$MLANG_BIN" ]]; then
  log_error "mlang binary not found at $MLANG_BIN"
  log_error "build it first (e.g., cmake --build build)"
  exit 1
fi

PYTHON_BIN="${PYTHON_BIN:-python3}"
if [[ -x "$ROOT_DIR/.venv/bin/python3" ]]; then
  PYTHON_BIN="$ROOT_DIR/.venv/bin/python3"
elif [[ -x "$ROOT_DIR/.venv/bin/python" ]]; then
  PYTHON_BIN="$ROOT_DIR/.venv/bin/python"
fi

ROBOT_CMD=()
PABOT_CMD=()

if [[ -n "${VIRTUAL_ENV:-}" && -x "$VIRTUAL_ENV/bin/robot" ]]; then
  ROBOT_CMD=("$VIRTUAL_ENV/bin/robot")
elif [[ -x "$ROOT_DIR/.venv/bin/robot" ]]; then
  ROBOT_CMD=("$ROOT_DIR/.venv/bin/robot")
else
  ROBOT_CMD=("$PYTHON_BIN" "-m" "robot")
fi
if [[ -n "${VIRTUAL_ENV:-}" && -x "$VIRTUAL_ENV/bin/pabot" ]]; then
  PABOT_CMD=("$VIRTUAL_ENV/bin/pabot")
elif [[ -x "$ROOT_DIR/.venv/bin/pabot" ]]; then
  PABOT_CMD=("$ROOT_DIR/.venv/bin/pabot")
else
  PABOT_CMD=("$PYTHON_BIN" "-m" "pabot")
fi

if [[ "${ROBOT_DEBUG:-0}" == "1" ]]; then
  log_debug "ROBOT_CMD=${ROBOT_CMD[*]}"
  log_debug "PABOT_CMD=${PABOT_CMD[*]}"
fi

rc=0
if ! "${ROBOT_CMD[@]}" --version >/dev/null 2>&1; then
  rc=$?
fi
if [[ ${rc} -ne 0 && ${rc} -ne 251 ]]; then
  ROBOT_CMD=()
fi
rc=0
if ! "${PABOT_CMD[@]}" --version >/dev/null 2>&1; then
  rc=$?
fi
if [[ ${rc} -ne 0 && ${rc} -ne 251 ]]; then
  PABOT_CMD=()
fi

if [[ ${#ROBOT_CMD[@]} -eq 0 ]]; then
  log_info "this script depends on Robot Framework."
  log_error "Robot Framework not found."
  log_error "install with: python3 -m pip install -r tests/requirements.txt"
  log_error "venv setup:"
  log_error "  python3 -m venv .venv"
  log_error "  source .venv/bin/activate"
  log_error "  python -m pip install -r tests/requirements.txt"
  log_debug "VIRTUAL_ENV=${VIRTUAL_ENV:-<unset>}"
  log_debug "PYTHON_BIN=$PYTHON_BIN"
  exit 1
fi

CONSOLE_MODE="${ROBOT_CONSOLE_MODE:-dotted}"
CONSOLE_MODE_EXPLICIT=0
if [[ -n "${ROBOT_CONSOLE_MODE:-}" ]]; then
  CONSOLE_MODE_EXPLICIT=1
fi
if [[ -n "${ROBOT_CONSOLE_COLORS:-}" ]]; then
  CONSOLE_COLORS="${ROBOT_CONSOLE_COLORS}"
else
  case "$COLOR_LOGS" in
    always) CONSOLE_COLORS="ansi" ;;
    never) CONSOLE_COLORS="off" ;;
    *) CONSOLE_COLORS="auto" ;;
  esac
fi
if [[ -n "${ROBOT_CONSOLE_WIDTH:-}" ]]; then
  CONSOLE_WIDTH="${ROBOT_CONSOLE_WIDTH}"
else
  if command -v tput >/dev/null 2>&1; then
    CONSOLE_WIDTH="$(tput cols 2>/dev/null || echo 100)"
  else
    CONSOLE_WIDTH="100"
  fi
fi
if ! [[ "$CONSOLE_WIDTH" =~ ^[0-9]+$ ]] || [[ "$CONSOLE_WIDTH" -lt 40 ]]; then
  CONSOLE_WIDTH="100"
fi
log_verbose "ROBOT_CONSOLE_MODE=$CONSOLE_MODE ROBOT_CONSOLE_COLORS=$CONSOLE_COLORS ROBOT_CONSOLE_WIDTH=$CONSOLE_WIDTH"

RUN_CMD=("${ROBOT_CMD[@]}")
USE_INTERNAL_PARALLEL=0
if [[ "$ROBOT_JOBS" -gt 1 ]]; then
  if [[ -f "$PARALLEL_RUNNER" ]]; then
    USE_INTERNAL_PARALLEL=1
  else
    if [[ ${#PABOT_CMD[@]} -eq 0 ]]; then
      log_error "parallel robot jobs requested (-j $ROBOT_JOBS) but no parallel runner is available."
      log_error "falling back to serial robot run (jobs=1)."
      log_error "install with: python3 -m pip install -r tests/requirements.txt"
      ROBOT_JOBS=1
    else
      if [[ -f "$PABOT_ROBOT_WRAPPER" ]]; then
        RUN_CMD=("${PABOT_CMD[@]}" --processes "$ROBOT_JOBS" --testlevelsplit --no-pabotlib --command "$PYTHON_BIN" "$PABOT_ROBOT_WRAPPER" --end-command)
      else
        RUN_CMD=("${PABOT_CMD[@]}" --processes "$ROBOT_JOBS" --testlevelsplit --no-pabotlib --command "${ROBOT_CMD[@]}" --end-command)
      fi
      if [[ "$CONSOLE_MODE_EXPLICIT" -eq 0 ]]; then
        # Keep pabot worker chatter low; listener provides live progress.
        CONSOLE_MODE="quiet"
      fi
    fi
  fi
fi

log_info "running robot tests with jobs=$ROBOT_JOBS"
if [[ -f "$PROGRESS_LISTENER" ]]; then
  log_info "robot progress listener enabled"
fi

mkdir -p "$ROBOT_RESULTS_DIR"

start_ts="$(date +%s)"
set +e
if [[ "$ROBOT_JOBS" -gt 1 ]]; then
  if [[ "$USE_INTERNAL_PARALLEL" -eq 1 ]]; then
    ROBOT_TIME_FORMAT="$ROBOT_TIME_FORMAT" \
    PYTHONUNBUFFERED=1 "$PYTHON_BIN" "$PARALLEL_RUNNER" \
      --jobs "$ROBOT_JOBS" \
      --mlang "$MLANG_BIN" \
      --suite "$ROOT_DIR/tests/robot/examples.robot" \
      --repo-root "$ROOT_DIR" \
      --outputdir "$ROBOT_RESULTS_DIR" \
      --time-format "$ROBOT_TIME_FORMAT" \
      --color "$COLOR_LOGS" \
      --python-bin "$PYTHON_BIN" \
      $([[ "$ROBOT_SHOW_RUN" == "1" ]] && echo "--show-run") \
      $([[ "$ROBOT_ALLOW_FLAKY_PASS" == "1" ]] && echo "--allow-flaky-pass")
    rc=$?
  else
    total_tests="$(detect_robot_total_tests)"
    if [[ -f "$PABOT_FILTER" ]]; then
      ROBOT_TIME_FORMAT="$ROBOT_TIME_FORMAT" \
      MLANG_PABOT_ARTIFACT_ROOT="$ROBOT_RESULTS_DIR/worker_artifacts" \
      PYTHONUNBUFFERED=1 "${RUN_CMD[@]}" \
        --console "$CONSOLE_MODE" \
        --consolecolors "$CONSOLE_COLORS" \
        --consolewidth "$CONSOLE_WIDTH" \
        --variable MLANG:"$MLANG_BIN" \
        --outputdir "$ROBOT_RESULTS_DIR" \
        "$ROOT_DIR/tests/robot/examples.robot" 2>&1 | \
        "$PYTHON_BIN" "$PABOT_FILTER" --total "$total_tests" --color "$COLOR_LOGS" --time-format "$ROBOT_TIME_FORMAT"
      rc=${PIPESTATUS[0]}
    else
      ROBOT_TIME_FORMAT="$ROBOT_TIME_FORMAT" \
      MLANG_PABOT_ARTIFACT_ROOT="$ROBOT_RESULTS_DIR/worker_artifacts" \
      PYTHONUNBUFFERED=1 "${RUN_CMD[@]}" \
        --console "$CONSOLE_MODE" \
        --consolecolors "$CONSOLE_COLORS" \
        --consolewidth "$CONSOLE_WIDTH" \
        --variable MLANG:"$MLANG_BIN" \
        --outputdir "$ROBOT_RESULTS_DIR" \
        "$ROOT_DIR/tests/robot/examples.robot"
      rc=$?
    fi
  fi
else
  ROBOT_TIME_FORMAT="$ROBOT_TIME_FORMAT" \
  PYTHONUNBUFFERED=1 "${RUN_CMD[@]}" \
    --console "$CONSOLE_MODE" \
    --consolecolors "$CONSOLE_COLORS" \
    --consolewidth "$CONSOLE_WIDTH" \
    --listener "$PROGRESS_LISTENER" \
    --variable MLANG:"$MLANG_BIN" \
    --outputdir "$ROBOT_RESULTS_DIR" \
    "$ROOT_DIR/tests/robot/examples.robot"
  rc=$?
fi
set -e
end_ts="$(date +%s)"
elapsed="$((end_ts - start_ts))"
elapsed_min="$((elapsed / 60))"
elapsed_sec="$((elapsed % 60))"
print_robot_failure_summary "$ROBOT_RESULTS_DIR/output.xml"
log_info "robot run finished: ${elapsed_min}m ${elapsed_sec}s (rc=${rc})"
exit "$rc"
