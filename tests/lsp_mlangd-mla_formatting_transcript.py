#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_formatting_") as td:
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

            fmt_tabs = client.request(
                "textDocument/formatting",
                {
                    "textDocument": {"uri": uri},
                    "options": {"tabSize": 4, "insertSpaces": False},
                },
            )
            assert isinstance(fmt_tabs, list) and fmt_tabs, f"formatting(insertSpaces=false) should return edits: {fmt_tabs!r}"
            tabs_text = fmt_tabs[0].get("newText", "")
            assert "\tlet x: i32 = 1;\n" in tabs_text, f"expected tabs preserved when insertSpaces=false: {fmt_tabs!r}"
            assert "let x: i32 = 1;   " not in tabs_text, f"expected trailing spaces trimmed with insertSpaces=false: {fmt_tabs!r}"

            rfmt = client.request(
                "textDocument/rangeFormatting",
                {
                    "textDocument": {"uri": uri},
                    "range": {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 19}},
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(rfmt, list), f"rangeFormatting should return array: {rfmt!r}"
            assert rfmt, f"rangeFormatting should return at least one edit: {rfmt!r}"
            rfirst = rfmt[0]
            rnew = rfirst.get("newText", "")
            assert "\t" not in rnew, f"expected tab normalization in range formatting output: {rfirst!r}"
            rr = rfirst.get("range", {})
            rs = rr.get("start", {})
            re = rr.get("end", {})
            assert rs.get("line") == 1 and rs.get("character") == 0, f"rangeFormatting start mismatch: {rfirst!r}"
            assert re.get("line") == 1 and re.get("character") == 19, f"rangeFormatting end mismatch: {rfirst!r}"
            assert rnew.endswith(";"), f"expected trailing spaces trimmed in range formatting output: {rfirst!r}"

            # On-type formatting: Enter after '{' should indent next line.
            text_brace = (
                "fn main() -> i32 {\n"
                "\n"
                "}\n"
            )
            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": text_brace}],
                },
            )
            on_type_brace = client.request(
                "textDocument/onTypeFormatting",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": 1, "character": 0},
                    "ch": "\n",
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(on_type_brace, list), f"onTypeFormatting should return array: {on_type_brace!r}"
            assert on_type_brace, f"onTypeFormatting should return edit after '{{': {on_type_brace!r}"
            brace_edit = on_type_brace[0]
            assert brace_edit.get("newText", "") == "    ", f"expected 4-space indent after '{{': {brace_edit!r}"

            # On-type formatting: Enter after '(' should continuation-indent.
            text_paren = (
                "fn main() -> i32 {\n"
                "    some(\n"
                "\n"
                "    );\n"
                "}\n"
            )
            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 3},
                    "contentChanges": [{"text": text_paren}],
                },
            )
            on_type_paren = client.request(
                "textDocument/onTypeFormatting",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": 2, "character": 0},
                    "character": "\n",
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(on_type_paren, list), f"onTypeFormatting should return array: {on_type_paren!r}"
            assert on_type_paren, f"onTypeFormatting should return edit after '(': {on_type_paren!r}"
            paren_edit = on_type_paren[0]
            assert paren_edit.get("newText", "") == "        ", f"expected continuation indent after '(': {paren_edit!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla formatting transcript (document + range + onType)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
