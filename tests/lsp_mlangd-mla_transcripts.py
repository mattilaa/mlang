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
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    py = sys.executable
    run_step([py, "tests/lsp_mlangd-mla_codeaction_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_rename_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_quickfix_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_formatting_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_completion_docs_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_completion_symbol_docs_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_diagnostic_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_workspace_diagnostic_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_incremental_didchange_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_progress_cancel_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_semantic_tokens_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_multifile_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_member_definition_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_internal_definition_transcript.py", "--mlangd", str(mlangd)])
    run_step([py, "tests/lsp_mlangd-mla_c_extern_definition_transcript.py", "--mlangd", str(mlangd)])
    print("PASS: mlangd-mla transcript suite")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
