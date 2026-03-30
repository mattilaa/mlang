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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_hover_") as td:
        root = Path(td)
        file_path = root / "hover_struct_size.mla"
        uri = to_uri(file_path)
        text = (
            "struct DeviceFlags {\n"
            "  var power: bit;\n"
            "  var armed: bool;\n"
            "};\n"
            "fn main() -> i32 {\n"
            "  let d: DeviceFlags = DeviceFlags { power: 1, armed: true };\n"
            "  return 0;\n"
            "}\n"
        )
        file_path.write_text(text)
        line, character = position_of(text, "DeviceFlags", 1)

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

            hover = client.request(
                "textDocument/hover",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": line, "character": character},
                },
            )
            assert isinstance(hover, dict), f"hover should return object: {hover!r}"
            contents = hover.get("contents")
            assert isinstance(contents, dict), f"hover contents should be object: {hover!r}"
            value = contents.get("value", "")
            assert isinstance(value, str) and value, f"hover value missing: {hover!r}"
            assert "symbol: DeviceFlags [struct]" in value, f"struct hover missing symbol header: {value!r}"
            assert "size=2 bytes" in value, f"struct hover missing size summary: {value!r}"
            assert "power: bit (1B)" in value, f"struct hover missing bit field layout: {value!r}"
            assert "armed: bool (1B)" in value, f"struct hover missing bool field layout: {value!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla hover transcript (struct layout summary)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
