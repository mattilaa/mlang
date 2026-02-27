#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_incremental_") as td:
        root = Path(td)
        file_path = root / "inc_case.mla"
        uri = to_uri(file_path)

        text_v1 = (
            "fn add(a: i32, b: i32) -> i32 {\n"
            "    return a + b;\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    return ad(1, 2);\n"
            "}\n"
        )
        file_path.write_text(text_v1)

        call_line, call_char = position_of(text_v1, "ad(")

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
                        "text": text_v1,
                    }
                },
            )

            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [
                        {
                            "range": {
                                "start": {"line": call_line, "character": call_char},
                                "end": {"line": call_line, "character": call_char + 2},
                            },
                            "rangeLength": 2,
                            "text": "add",
                        }
                    ],
                },
            )

            defn = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": call_line, "character": call_char + 1},
                },
            )
            assert isinstance(defn, list), f"definition should return array: {defn!r}"
            assert defn, f"definition should resolve after incremental change: {defn!r}"
            loc = defn[0]
            rng = loc.get("range", {})
            start = rng.get("start", {})
            assert start.get("line") == 0, f"expected definition on first line: {loc!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla incremental didChange transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
