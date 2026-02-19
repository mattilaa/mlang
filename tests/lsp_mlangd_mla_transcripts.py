#!/usr/bin/env python3
import argparse
import subprocess
import sys
from pathlib import Path


def run_step(cmd: list[str]) -> None:
    print(f"RUN: {' '.join(cmd)}")
    rc = subprocess.run(cmd).returncode
    if rc != 0:
        raise SystemExit(rc)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    py = sys.executable
    run_step([py, "tests/lsp_mlangd_mla_codeaction_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd_mla_rename_transcript.py", "--mlangd", str(mlangd)])
    print("PASS: mlangd_mla transcript suite")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
