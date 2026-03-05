#!/usr/bin/env python3
"""Robot Framework progress listener with keyword-only color."""

from __future__ import annotations

import os
import sys
import time
from datetime import datetime

ROBOT_LISTENER_API_VERSION = 3

_RESET = "\033[0m"
_COLORS = {
    "PROGRESS": "\033[36m",
    "RUN": "\033[34m",
    "PASS": "\033[32m",
    "FAIL": "\033[31m",
    "SKIP": "\033[33m",
}
_START_TIMES = {}
_ACTIVE_TEST_STACK = []
_DOT_COUNT = {}
_TEST_IDX = 0
_TOTAL_TESTS = 0
_STATUS_WIDTH = 4
_TIME_FORMAT = os.getenv("ROBOT_TIME_FORMAT", "full").lower()
if _TIME_FORMAT not in {"full", "hms", "off"}:
    _TIME_FORMAT = "full"


def _color_enabled() -> bool:
    robot_color_mode = os.getenv("ROBOT_COLOR_LOGS", "").lower()
    if robot_color_mode == "always":
        return True
    if robot_color_mode == "never":
        return False
    if os.getenv("NO_COLOR"):
        return False
    if os.getenv("CLICOLOR") == "0":
        return False
    if os.getenv("CLICOLOR_FORCE", "0") != "0":
        return True
    term = os.getenv("TERM", "")
    if term == "dumb":
        return False
    return sys.stdout.isatty()


_USE_COLOR = _color_enabled()


def _kw(word: str) -> str:
    if not _USE_COLOR:
        return word
    color = _COLORS.get(word, "")
    if not color:
        return word
    return f"{color}{word}{_RESET}"

def _progress_token(idx: int, total: int) -> str:
    digits = len(str(total)) if total > 0 else 1
    token = f"[{idx:>{digits}}/{total}]" if total > 0 else f"[{idx:>{digits}}]"
    if _USE_COLOR:
        return f"{_COLORS['PROGRESS']}{token}{_RESET}"
    return token

def _status_tag(word: str) -> str:
    rendered = f"{word:<{_STATUS_WIDTH}}"
    if _USE_COLOR and word in _COLORS:
        rendered = f"{_COLORS[word]}{rendered}{_RESET}"
    return f"[{rendered}]"

def _timestamp_now() -> str:
    if _TIME_FORMAT == "off":
        return ""
    dt = datetime.now()
    if _TIME_FORMAT == "hms":
        return dt.strftime("%H:%M:%S")
    return dt.strftime("%m/%d/%Y %H:%M:%S.") + f"{int(dt.microsecond / 1000):03d}"


def _fmt_seconds(ms) -> str:
    try:
        if ms is not None:
            return f"{(float(ms) / 1000.0):.3f}s"
    except Exception:
        pass
    return "0.000s"

def start_suite(data, result):
    global _TOTAL_TESTS
    count = getattr(result, "test_count", None)
    if count is None:
        count = getattr(result, "testcount", None)
    try:
        if count is not None and int(count) > 0:
            _TOTAL_TESTS = int(count)
    except Exception:
        pass


def start_test(data, result):
    global _TEST_IDX
    tid = id(data)
    _TEST_IDX += 1
    _START_TIMES[tid] = time.perf_counter()
    _ACTIVE_TEST_STACK.append(tid)
    _DOT_COUNT[tid] = 0
    ts = _timestamp_now()
    ts_prefix = f"{ts} " if ts else ""
    sys.stdout.write(
        f"{_progress_token(_TEST_IDX, _TOTAL_TESTS)} {ts_prefix}{_status_tag('RUN')} {result.full_name} "
    )
    sys.stdout.flush()


def start_keyword(data, result):
    if not _ACTIVE_TEST_STACK:
        return
    tid = _ACTIVE_TEST_STACK[-1]
    _DOT_COUNT[tid] = _DOT_COUNT.get(tid, 0) + 1
    sys.stdout.write(".")
    sys.stdout.flush()


def end_test(data, result):
    tid = id(data)
    if _ACTIVE_TEST_STACK and _ACTIVE_TEST_STACK[-1] == tid:
        _ACTIVE_TEST_STACK.pop()
    dot_count = _DOT_COUNT.pop(tid, 0)
    elapsed = None
    started = _START_TIMES.pop(tid, None)
    if started is not None:
        elapsed = f"{(time.perf_counter() - started):.3f}s"
    else:
        elapsed_ms = getattr(result, "elapsed_time", None)
        if elapsed_ms is None:
            elapsed_ms = getattr(result, "elapsedtime", None)
        elapsed = _fmt_seconds(elapsed_ms)
    status = (getattr(result, "status", "") or "UNKNOWN").upper()
    status_kw = _status_tag(status) if status in _COLORS else f"[{status}]"
    sys.stdout.write(f"{status_kw} ({elapsed})\n")
    sys.stdout.write("\n")
    sys.stdout.flush()
