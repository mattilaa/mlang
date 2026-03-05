#!/usr/bin/env python3
"""Compact progress formatter for pabot output."""

from __future__ import annotations

import argparse
from datetime import datetime
import os
import re
import sys

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
LINE_RE = re.compile(
    r"\[ID:(\d+)\]\s+(PASSED|FAILED|SKIPPED)\s+(.+?)(?:\s+in\s+([0-9.]+)\s+seconds)?$"
)
EXEC_RE = re.compile(
    r"\[ID:(\d+)\]\s+EXECUTING(?:\s+PARALLEL)?\s+(.+?)(?::)?$"
)
STILL_RUNNING_RE = re.compile(
    r"\[ID:(\d+)\]\s+still running\s+(.+?)\s+after\s+([0-9.]+)s$"
)
TS_RE = re.compile(r"^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{1,6})")
FAIL_REASON_RE = re.compile(r"^\s*(?:\|\s*FAIL\s*\||FAIL(?:URE)?[: ]|Message:\s*)(.+)$")

STATUS_MAP = {"PASSED": "PASS", "FAILED": "FAIL", "SKIPPED": "SKIP"}
COLORS = {
    "PROGRESS": "\033[36m",
    "RUN": "\033[34m",
    "PASS": "\033[32m",
    "FAIL": "\033[31m",
    "SKIP": "\033[33m",
}
RESET = "\033[0m"


def color_enabled(mode: str) -> bool:
    mode = mode.lower()
    if mode == "always":
        return True
    if mode == "never":
        return False
    if os.getenv("NO_COLOR"):
        return False
    if os.getenv("CLICOLOR") == "0":
        return False
    if os.getenv("CLICOLOR_FORCE", "0") != "0":
        return True
    return sys.stdout.isatty()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--total", type=int, default=0)
    ap.add_argument("--color", default="auto")
    ap.add_argument("--time-format", choices=["full", "hms", "off"], default="full")
    args = ap.parse_args()

    use_color = color_enabled(args.color)
    id_to_idx: dict[str, int] = {}
    next_idx = 1
    failed_tests: list[str] = []
    pending_fail = None  # (progress, test_name, elapsed_ms)

    digits = len(str(args.total)) if args.total > 0 else 1
    status_width = 4

    def fmt_ts(ts_src: str) -> str:
        if args.time_format == "off":
            return ""
        try:
            dt = datetime.strptime(ts_src, "%Y-%m-%d %H:%M:%S.%f")
            if args.time_format == "hms":
                return dt.strftime("%H:%M:%S")
            return dt.strftime("%m/%d/%Y %H:%M:%S.") + f"{int(dt.microsecond / 1000):03d}"
        except Exception:
            return ""

    def status_tag(word: str) -> str:
        padded = f"{word:<{status_width}}"
        colored = padded
        if use_color and word in COLORS:
            colored = f"{COLORS[word]}{padded}{RESET}"
        return f"[{colored}]"

    def flush_pending_fail(reason: str = ""):
        nonlocal pending_fail
        if pending_fail is None:
            return
        progress, ts_fmt, test_name, elapsed_ms = pending_fail
        suffix = f" ({elapsed_ms}ms)" if elapsed_ms is not None else ""
        reason_txt = f" - {reason}" if reason else ""
        ts_prefix = f"{ts_fmt} " if ts_fmt else ""
        sys.stdout.write(
            f"{progress} {ts_prefix}{status_tag('FAIL')} {test_name}{suffix}{reason_txt}\n"
        )
        sys.stdout.flush()
        pending_fail = None

    for raw in sys.stdin:
        raw_line = raw.rstrip("\n")
        line = ANSI_RE.sub("", raw_line)

        rm = FAIL_REASON_RE.match(line)
        if rm and pending_fail is not None:
            flush_pending_fail(rm.group(1).strip())
            continue

        tm = TS_RE.search(line)
        ts_fmt = fmt_ts(tm.group(1)) if tm else fmt_ts("")

        em = EXEC_RE.search(line)
        if em:
            flush_pending_fail()
            tid = em.group(1)
            test_name = em.group(2)
            if tid not in id_to_idx:
                id_to_idx[tid] = next_idx
                next_idx += 1
            idx = id_to_idx[tid]
            progress = (
                f"[{idx:>{digits}}/{args.total}]"
                if args.total > 0
                else f"[{idx:>{digits}}]"
            )
            if use_color:
                progress = f"{COLORS['PROGRESS']}{progress}{RESET}"
            ts_prefix = f"{ts_fmt} " if ts_fmt else ""
            sys.stdout.write(f"{progress} {ts_prefix}{status_tag('RUN')} {test_name}\n")
            sys.stdout.flush()
            continue

        sm = STILL_RUNNING_RE.search(line)
        if sm:
            # Keep output compact; the [RUN] line already indicates active tests.
            continue

        m = LINE_RE.search(line)
        if m:
            flush_pending_fail()
            tid = m.group(1)
            if tid not in id_to_idx:
                id_to_idx[tid] = next_idx
                next_idx += 1
            idx = id_to_idx[tid]
            status = STATUS_MAP.get(m.group(2), m.group(2))
            test_name = m.group(3)
            seconds = m.group(4)
            elapsed_ms = None
            if seconds is not None:
                try:
                    elapsed_ms = int(round(float(seconds) * 1000.0))
                except Exception:
                    elapsed_ms = None
            progress = (
                f"[{idx:>{digits}}/{args.total}]"
                if args.total > 0
                else f"[{idx:>{digits}}]"
            )
            if use_color:
                progress = f"{COLORS['PROGRESS']}{progress}{RESET}"
            if status == "FAIL":
                failed_tests.append(test_name)
                pending_fail = (progress, ts_fmt, test_name, elapsed_ms)
            else:
                suffix = f" ({elapsed_ms}ms)" if elapsed_ms is not None else ""
                sys.stdout.write(
                    f"{progress} {f'{ts_fmt} ' if ts_fmt else ''}{status_tag(status)} {test_name}{suffix}\n"
                )
                sys.stdout.flush()
            continue

        if (
            line.startswith("Output:  ")
            or line.startswith("Running suite '")
            or line.startswith("robot -A ")
            or line.startswith("PASSED ")
            or line.startswith("FAILED ")
            or line.startswith("All tests passed")
        ):
            continue

        flush_pending_fail()
        sys.stdout.write(raw_line + "\n")
        sys.stdout.flush()

    flush_pending_fail()
    if failed_tests:
        uniq = sorted(set(failed_tests))
        sys.stdout.write(f"\nFAIL SUMMARY ({len(uniq)}):\n")
        for name in uniq:
            sys.stdout.write(f"- {name}\n")
        sys.stdout.flush()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
