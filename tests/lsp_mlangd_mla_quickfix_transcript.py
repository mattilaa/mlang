#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_quickfix_") as td:
        root = Path(td)
        file_path = root / "missing_semicolon.mla"
        uri = to_uri(file_path)
        text = (
            "fn main() -> i32 {\n"
            "  let x: i32 = 1\n"
            "  return x;\n"
            "}\n"
        )
        file_path.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})
            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": text,
                    }
                },
            )

            line, one_char = position_of(text, "1")
            semicolon_diag = {
                "range": {
                    "start": {"line": line, "character": one_char + 1},
                    "end": {"line": line, "character": one_char + 1},
                },
                "severity": 1,
                "message": "expected ';' after expression",
            }

            ca = client.request(
                "textDocument/codeAction",
                {
                    "textDocument": {"uri": uri},
                    "range": semicolon_diag.get("range", {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 0}}),
                    "context": {"diagnostics": [semicolon_diag], "only": ["quickfix"]},
                },
            )
            assert isinstance(ca, list) and ca, f"expected quickfix codeAction list: {ca!r}"

            quickfix = next((a for a in ca if a.get("kind") == "quickfix"), None)
            assert quickfix is not None, f"quickfix action missing: {ca!r}"
            changes = quickfix.get("edit", {}).get("changes", {}).get(uri, [])
            assert changes, f"quickfix has no edits: {quickfix!r}"
            edit = changes[0]
            assert edit.get("newText") == ";", f"expected semicolon insertion edit, got: {edit!r}"

            start = edit.get("range", {}).get("start", {})
            end = edit.get("range", {}).get("end", {})
            assert start.get("line") == end.get("line"), f"expected point insertion range: {edit!r}"
            assert start.get("character") == end.get("character"), f"expected point insertion range: {edit!r}"
            assert start.get("line") == line, f"unexpected insertion line: {edit!r}"
            assert start.get("character") >= one_char + 1, f"insertion should occur after expression: {edit!r}"
        finally:
            client.close()

    print("PASS: mlangd_mla quickfix transcript (missing semicolon)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
