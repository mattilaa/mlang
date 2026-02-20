#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def open_doc(client: JsonRpcClient, path: Path, text: str, version: int = 1) -> None:
    client.notify(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": to_uri(path),
                "languageId": "mlang",
                "version": version,
                "text": text,
            }
        },
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_c_extern_def_") as td:
        root = Path(td)
        doc = root / "extern_def.mla"
        c_src = root / "native.c"
        text = (
            "extern fn c_add(a: i32, b: i32) -> i32;\n"
            "\n"
            "fn main() -> i32 {\n"
            "  return c_add(2, 3);\n"
            "}\n"
        )
        c_text = (
            "#include <stdint.h>\n"
            "\n"
            "int32_t c_add(int32_t a, int32_t b) {\n"
            "  return a + b;\n"
            "}\n"
        )
        doc.write_text(text)
        c_src.write_text(c_text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})
            open_doc(client, doc, text)

            line, char = position_of(text, "c_add(a:")
            res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": line, "character": char},
                },
            )
            assert isinstance(res, list) and res, f"expected C definition result: {res!r}"
            target = res[0]
            assert target.get("uri") == to_uri(c_src), f"expected jump to native.c: {res!r}"
            start = target.get("range", {}).get("start", {})
            assert start.get("line") == 2, f"expected c_add on line 3 in C source: {res!r}"

            call_line, call_char = position_of(text, "c_add(2, 3)")
            call_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": call_line, "character": call_char},
                },
            )
            assert isinstance(call_res, list) and call_res, f"expected C definition from call site: {call_res!r}"
            call_target = call_res[0]
            assert call_target.get("uri") == to_uri(c_src), f"expected call-site jump to native.c: {call_res!r}"
            call_start = call_target.get("range", {}).get("start", {})
            assert call_start.get("line") == 2, f"expected call-site c_add on line 3 in C source: {call_res!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla C extern definition transcript (extern fn -> C source)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
