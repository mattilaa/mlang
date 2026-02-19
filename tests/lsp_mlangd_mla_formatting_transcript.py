#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_formatting_") as td:
        root = Path(td)
        file_path = root / "format_case.mla"
        uri = to_uri(file_path)
        text = (
            "fn main() -> i32 {\n"
            "\tlet x: i32 = 1;   \n"
            "  return x; \n"
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

            fmt = client.request(
                "textDocument/formatting",
                {
                    "textDocument": {"uri": uri},
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(fmt, list), f"formatting should return array: {fmt!r}"
            assert fmt, f"formatting should return at least one edit for unformatted text: {fmt!r}"
            first = fmt[0]
            new_text = first.get("newText", "")
            assert "    let x: i32 = 1;\n" in new_text, f"expected tab normalization in formatting output: {first!r}"
            assert "return x; \n" not in new_text, f"expected trailing whitespace trimmed: {first!r}"

            rfmt = client.request(
                "textDocument/rangeFormatting",
                {
                    "textDocument": {"uri": uri},
                    "range": {"start": {"line": 1, "character": 0}, "end": {"line": 2, "character": 0}},
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(rfmt, list), f"rangeFormatting should return array: {rfmt!r}"
            assert rfmt, f"rangeFormatting should return at least one edit: {rfmt!r}"
            rfirst = rfmt[0]
            rnew = rfirst.get("newText", "")
            assert "\t" not in rnew, f"expected tab normalization in range formatting output: {rfirst!r}"
        finally:
            client.close()

    print("PASS: mlangd_mla formatting transcript (document + range)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
