#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="build/mlangd")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_format_cfg_") as td:
        root = Path(td)
        src_dir = root / "src"
        src_dir.mkdir(parents=True, exist_ok=True)

        file_path = src_dir / "format_case.mla"
        uri = to_uri(file_path)
        text = (
            "fn main()->i32{\n"
            "\tlet x:i32=1+2;\n"
            "\tlet y:i32=foo(1,2);\n"
            "\treturn x+y;\n"
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

            fmt_default = client.request(
                "textDocument/formatting",
                {
                    "textDocument": {"uri": uri},
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(fmt_default, list) and fmt_default, (
                f"default formatting should return edits: {fmt_default!r}"
            )
            default_text = fmt_default[0].get("newText", "")
            assert "let x: i32 = 1 + 2;" in default_text, (
                f"default formatting should use default spaces: {fmt_default!r}"
            )
            assert "let y: i32 = foo(1, 2);" in default_text, (
                f"default formatting should use default comma spacing: {fmt_default!r}"
            )
            assert not default_text.endswith("\n"), (
                "default formatting should respect EnsureTrailingNewline: false"
            )

            (root / ".mlang-format").write_text(
                "BasedOnStyle: Rust\n"
                "IndentWidth: 2\n"
                "SpaceAfterComma: false\n"
                "SpaceAfterColon: false\n"
                "SpaceAroundOperators: false\n"
                "EnsureTrailingNewline: true\n"
            )

            fmt_cfg = client.request(
                "textDocument/formatting",
                {
                    "textDocument": {"uri": uri},
                    "options": {"tabSize": 4, "insertSpaces": True},
                },
            )
            assert isinstance(fmt_cfg, list) and fmt_cfg, (
                f"configured formatting should return edits: {fmt_cfg!r}"
            )
            cfg_text = fmt_cfg[0].get("newText", "")

            assert "  let x:i32=1+2;" in cfg_text, (
                f"configured formatting should apply IndentWidth/spacing: {fmt_cfg!r}"
            )
            assert "  let y:i32=foo(1,2);" in cfg_text, (
                f"configured formatting should apply comma/colon spacing: {fmt_cfg!r}"
            )
            assert "  return x+y;" in cfg_text, (
                f"configured formatting should remove operator spacing: {fmt_cfg!r}"
            )
            assert cfg_text.endswith("\n"), (
                "configured formatting should apply EnsureTrailingNewline: true"
            )
        finally:
            client.close()

    print("PASS: mlangd formatting transcript honors .mlang-format end-to-end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
