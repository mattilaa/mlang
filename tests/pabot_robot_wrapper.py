#!/usr/bin/env python3
"""Wrapper used by pabot to run robot with per-worker isolation env vars."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def _worker_token(env: dict[str, str]) -> str:
    return (
        env.get("PABOTEXECUTIONPOOLID")
        or env.get("PABOTQUEUEINDEX")
        or str(os.getpid())
    )


def main() -> int:
    env = dict(os.environ)
    token = _worker_token(env)
    pid = os.getpid()

    root = env.get("MLANG_PABOT_ARTIFACT_ROOT", "")
    if root and not env.get("MLANG_ARTIFACT_DIR"):
        artifact_dir = Path(root) / f"worker_{token}_{pid}"
        artifact_dir.mkdir(parents=True, exist_ok=True)
        env["MLANG_ARTIFACT_DIR"] = str(artifact_dir)

    # Avoid cross-worker races in mlang-pkg-mla temporary cache names.
    if not env.get("MLANG_PKG_CACHE_KEY"):
        env["MLANG_PKG_CACHE_KEY"] = f"{token}_{pid}"

    cmd = [sys.executable, "-m", "robot", *sys.argv[1:]]
    return subprocess.call(cmd, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
