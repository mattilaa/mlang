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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_decl_typedef_") as td:
        root = Path(td)
        file_path = root / "decl_typedef_case.mla"
        uri = to_uri(file_path)
        text = (
            "fn helper(v: i32) -> i32 {\n"
            "    return v;\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    let x: i32 = helper(1);\n"
            "    return x;\n"
            "}\n"
        )
        file_path.write_text(text)

        call_line, call_char = position_of(text, "helper(1)")
        type_line, type_char = position_of(text, "i32 = helper")

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            caps = init.get("capabilities", {})
            assert caps.get("declarationProvider") is True, f"declarationProvider missing: {caps!r}"
            assert caps.get("typeDefinitionProvider") is True, f"typeDefinitionProvider missing: {caps!r}"
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

            decl = client.request(
                "textDocument/declaration",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": call_line, "character": call_char + 1},
                },
            )
            assert isinstance(decl, list), f"declaration should return array: {decl!r}"
            assert decl, f"declaration should resolve helper declaration: {decl!r}"

            tdef = client.request(
                "textDocument/typeDefinition",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": type_line, "character": type_char + 1},
                },
            )
            assert isinstance(tdef, list), f"typeDefinition should return array: {tdef!r}"
            assert tdef, f"typeDefinition should resolve i32 type: {tdef!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla declaration + typeDefinition transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
