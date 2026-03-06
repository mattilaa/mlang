#!/usr/bin/env python3
"""Deterministic parallel runner for Robot tests without pabot."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from pathlib import Path


def _ts(fmt: str) -> str:
    if fmt == "off":
        return ""
    now = time.localtime()
    ms = int((time.time() % 1) * 1000)
    if fmt == "hms":
        return time.strftime("%H:%M:%S", now)
    return time.strftime("%m/%d/%Y %H:%M:%S", now) + f".{ms:03d}"


def _color_enabled(mode: str) -> bool:
    if mode == "always":
        return True
    if mode == "never":
        return False
    return sys.stdout.isatty() and os.getenv("TERM", "") != "dumb"


def _status_tag(status: str, use_color: bool) -> str:
    if not use_color:
        return f"[{status}]"
    colors = {
        "RUN ": "\x1b[34m",
        "PASS": "\x1b[32m",
        "FAIL": "\x1b[31m",
    }
    c = colors.get(status, "")
    if not c:
        return f"[{status}]"
    return f"[{c}{status}\x1b[0m]"


def _discover_tests(python_bin: str, suite: str, output_dir: Path) -> list[str]:
    dry_xml = output_dir / "dryrun_output.xml"
    cmd = [
        python_bin,
        "-m",
        "robot",
        "--dryrun",
        "--console",
        "none",
        "--log",
        "none",
        "--report",
        "none",
        "--output",
        str(dry_xml),
        suite,
    ]
    rc = subprocess.call(cmd)
    if rc != 0 or not dry_xml.exists():
        return []

    root = ET.parse(dry_xml).getroot()
    tests: list[str] = []

    def walk_suite(node: ET.Element, prefix: str) -> None:
        name = node.get("name", "").strip()
        full = name if not prefix else f"{prefix}.{name}"
        for t in node.findall("test"):
            tn = t.get("name", "").strip()
            if tn:
                tests.append(f"{full}.{tn}" if full else tn)
        for s in node.findall("suite"):
            walk_suite(s, full)

    top = root.find("suite")
    if top is not None:
        walk_suite(top, "")
    return tests


def _run_one(
    python_bin: str,
    suite: str,
    mlang_bin: str,
    repo_root: str,
    test_name: str,
    case_dir: Path,
    idx: int,
) -> tuple[int, str, float, Path]:
    case_dir.mkdir(parents=True, exist_ok=True)
    out_xml = case_dir / f"test_{idx}.xml"
    tmp_dir = case_dir / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    mlang_wrapper = case_dir / "mlang_case_wrapper.sh"
    wrapper_script = f"""#!/bin/sh
set -eu
export TMPDIR={str(tmp_dir)!r}
export TMP={str(tmp_dir)!r}
export TEMP={str(tmp_dir)!r}
if [ -z "${{HOME+x}}" ]; then
  export HOME={repo_root!r}
fi
exec {mlang_bin!r} "$@"
"""
    mlang_wrapper.write_text(wrapper_script, encoding="utf-8")
    mlang_wrapper.chmod(0o755)
    env = dict(os.environ)
    env["MLANG_ARTIFACT_DIR"] = str(case_dir / "artifacts")
    env["MLANG_PKG_CACHE_KEY"] = f"parallel_{idx}_{os.getpid()}"
    env["TMPDIR"] = str(tmp_dir)
    env["TMP"] = str(tmp_dir)
    env["TEMP"] = str(tmp_dir)
    cmd = [
        python_bin,
        "-m",
        "robot",
        "--console",
        "none",
        "--log",
        "none",
        "--report",
        "none",
        "--outputdir",
        str(case_dir),
        "--output",
        out_xml.name,
        "--variable",
        f"MLANG:{str(mlang_wrapper)}",
        "--test",
        test_name,
        suite,
    ]
    start = time.time()
    rc = subprocess.call(cmd, env=env, cwd=repo_root)
    elapsed = time.time() - start
    return rc, test_name, elapsed, out_xml


def _must_run_serial(test_name: str) -> bool:
    if test_name in {
        "Examples.Compile Errors For Conflicting Types",
        "Examples.Compile Errors For Reserved Type Keywords",
        "Examples.MLang Binary Frontend Env Switch Works",
        "Examples.Main Accepts Command Line Arguments",
        "Examples.Slice Example Runs Correctly",
        "Examples.Testing Mock Example Runs Correctly",
        "Examples.Argparser Demo Runs Correctly",
        "Examples.Closures Demo Runs Correctly",
        "Examples.Fs Lines Demo Runs Correctly",
        "Examples.Fs Seek Demo Runs Correctly",
        "Examples.Fs Rw Demo Runs Correctly",
        "Examples.Pkg PkgConfig Parity (CPP vs MLA)",
        "Examples.Lambda Fold Patterns Demo Runs Correctly",
        "Examples.Lambda Fold Advanced Demo Runs Correctly",
        "Examples.Pkg Fetch Build Parity (CPP vs MLA)",
        "Examples.Inline Attrs Demo Runs Correctly",
        "Examples.Multithreaded Net Server Client Roundtrip",
        "Examples.Printf And GetChar Demo",
        "Examples.Result Methods And Unwrap Warns",
    }:
        return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, required=True)
    ap.add_argument("--mlang", required=True)
    ap.add_argument("--suite", required=True)
    ap.add_argument("--outputdir", required=True)
    ap.add_argument("--time-format", choices=["full", "hms", "off"], default="full")
    ap.add_argument("--color", choices=["auto", "always", "never"], default="auto")
    ap.add_argument("--python-bin", default=sys.executable)
    ap.add_argument("--repo-root", default=os.getcwd())
    ap.add_argument("--allow-flaky-pass", action="store_true")
    ap.add_argument("--show-run", action="store_true")
    args = ap.parse_args()

    outdir = Path(args.outputdir)
    outdir.mkdir(parents=True, exist_ok=True)
    work = outdir / "parallel_cases"
    work.mkdir(parents=True, exist_ok=True)
    tests = _discover_tests(args.python_bin, args.suite, outdir)
    if not tests:
        print("No robot tests discovered", file=sys.stderr)
        return 1
    test_plan = [(i, tn) for i, tn in enumerate(tests, start=1)]
    parallel_plan = [(i, tn) for i, tn in test_plan if not _must_run_serial(tn)]
    serial_plan = [(i, tn) for i, tn in test_plan if _must_run_serial(tn)]

    use_color = _color_enabled(args.color)
    total = len(tests)
    finished = 0
    failed = 0
    first_status: dict[str, int] = {}
    latest_status: dict[str, int] = {}
    lock = threading.Lock()
    xmls: list[Path] = []

    def print_line(cur: int, status: str, name: str, elapsed: float | None = None) -> None:
        if status == "RUN " and not args.show_run:
            return
        prefix = f"[{cur:3d}/{total}]"
        ts = _ts(args.time_format)
        st = _status_tag(status, use_color)
        if elapsed is None:
            if ts:
                print(f"{prefix} {ts} {st} {name}", flush=True)
            else:
                print(f"{prefix} {st} {name}", flush=True)
            return
        dur = f"{elapsed:.3f}s"
        if ts:
            print(f"{prefix} {ts} {st} {name} ({dur})", flush=True)
        else:
            print(f"{prefix} {st} {name} ({dur})", flush=True)

    jobs = max(1, args.jobs)
    pending: set = set()
    parallel_iter = iter(parallel_plan)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for _ in range(min(jobs, len(parallel_plan))):
            i, tn = next(parallel_iter)
            print_line(i, "RUN ", tn)
            pending.add(
                ex.submit(
                    _run_one,
                    args.python_bin,
                    args.suite,
                    args.mlang,
                    args.repo_root,
                    tn,
                    work / f"case_{i}",
                    i,
                )
            )

        while pending:
            done, pending = wait(pending, return_when=FIRST_COMPLETED)
            for fut in done:
                rc, tn, elapsed, xml = fut.result()
                with lock:
                    finished += 1
                    cur = finished
                    if rc != 0:
                        failed += 1
                xmls.append(xml)
                if tn not in first_status:
                    first_status[tn] = rc
                latest_status[tn] = rc
                print_line(cur, "PASS" if rc == 0 else "FAIL", tn, elapsed)

                try:
                    i, next_t = next(parallel_iter)
                except StopIteration:
                    continue
                print_line(i, "RUN ", next_t)
                pending.add(
                    ex.submit(
                        _run_one,
                        args.python_bin,
                        args.suite,
                        args.mlang,
                        args.repo_root,
                        next_t,
                        work / f"case_{i}",
                        i,
                    )
                )

    for i, tn in serial_plan:
        print_line(i, "RUN ", tn)
        rc, tn, elapsed, xml = _run_one(
            args.python_bin,
            args.suite,
            args.mlang,
            args.repo_root,
            tn,
            work / f"case_{i}",
            i,
        )
        finished += 1
        if rc != 0:
            failed += 1
        xmls.append(xml)
        first_status[tn] = rc
        latest_status[tn] = rc
        print_line(finished, "PASS" if rc == 0 else "FAIL", tn, elapsed)

    failed_after_main = [tn for _i, tn in test_plan if latest_status.get(tn, 1) != 0]
    for tn in failed_after_main:
        i = next((idx for idx, name in test_plan if name == tn), 0)
        print_line(i, "RUN ", f"{tn} [rerun]")
        rc, _tn2, elapsed, xml = _run_one(
            args.python_bin,
            args.suite,
            args.mlang,
            args.repo_root,
            tn,
            work / f"rerun_case_{i}",
            i,
        )
        xmls.append(xml)
        latest_status[tn] = rc
        print_line(finished, "PASS" if rc == 0 else "FAIL", f"{tn} [rerun]", elapsed)

    # Verify flaky candidates sequentially once more to distinguish
    # transient flakiness from still-reproducible failures.
    flaky_candidates = [tn for tn in failed_after_main if latest_status.get(tn, 1) == 0]
    if flaky_candidates:
        print("")
        print(f"FLAKY CHECK CANDIDATES ({len(flaky_candidates)}):")
        for tn in flaky_candidates:
            print(f"- {tn}")

    flaky_verified: list[str] = []
    flaky_reproduced: list[str] = []
    for tn in flaky_candidates:
        i = next((idx for idx, name in test_plan if name == tn), 0)
        print_line(i, "RUN ", f"{tn} [flaky-check]")
        rc, _tn2, elapsed, xml = _run_one(
            args.python_bin,
            args.suite,
            args.mlang,
            args.repo_root,
            tn,
            work / f"flaky_check_case_{i}",
            i,
        )
        xmls.append(xml)
        if rc == 0:
            flaky_verified.append(tn)
        else:
            flaky_reproduced.append(tn)
            latest_status[tn] = rc
        print_line(
            finished,
            "PASS" if rc == 0 else "FAIL",
            f"{tn} [flaky-check]",
            elapsed,
        )

    failed = 0
    for _i, tn in test_plan:
        if latest_status.get(tn, 1) != 0:
            failed += 1
    flaky = [tn for _i, tn in test_plan if first_status.get(tn, 0) != 0 and latest_status.get(tn, 1) == 0]
    if flaky_verified:
        print("")
        print(f"FLAKY RETRY SUMMARY ({len(flaky_verified)}):")
        for tn in flaky_verified:
            print(f"- {tn}")
    if flaky_reproduced:
        print("")
        print(f"FLAKY FAIL SUMMARY ({len(flaky_reproduced)}):")
        for tn in flaky_reproduced:
            print(f"- {tn}")

    xmls = [x for x in xmls if x.exists()]
    if not xmls:
        print("No per-test xml outputs generated", file=sys.stderr)
        return 1

    merged_output = outdir / "output.xml"
    log_html = outdir / "log.html"
    report_html = outdir / "report.html"
    rebot_cmd = [
        args.python_bin,
        "-m",
        "robot.rebot",
        "--merge",
        "--output",
        str(merged_output),
        "--log",
        str(log_html),
        "--report",
        str(report_html),
        *[str(x) for x in xmls],
    ]
    rebot_rc = subprocess.call(rebot_cmd)
    if rebot_rc != 0 and failed == 0:
        return rebot_rc
    if args.allow_flaky_pass:
        return min(250, failed)
    return min(250, failed + len(flaky))


if __name__ == "__main__":
    raise SystemExit(main())
