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
            assert isinstance(call_res, list) and call_res, f"expected extern declaration from call site: {call_res!r}"
            call_target = call_res[0]
            assert call_target.get("uri") == to_uri(doc), f"expected call-site jump to extern declaration: {call_res!r}"
            call_start = call_target.get("range", {}).get("start", {})
            assert call_start.get("line") == 0, f"expected call-site c_add on line 1 in MLang source: {call_res!r}"

            libc_doc = root / "extern_libc.mla"
            libc_text = (
                "extern fn fopen(path: str8, mode: str8) -> ptr<void>;\n"
                "\n"
                "fn main() -> i32 {\n"
                "  let f: ptr<void> = fopen(\"/tmp/demo.txt\", \"rb\");\n"
                "  return 0;\n"
                "}\n"
            )
            libc_doc.write_text(libc_text)
            open_doc(client, libc_doc, libc_text, version=1)

            decl_line, decl_char = position_of(libc_text, "fopen(path:")
            libc_decl_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(libc_doc)},
                    "position": {"line": decl_line, "character": decl_char},
                },
            )
            assert isinstance(libc_decl_res, list) and libc_decl_res, (
                f"expected libc header definition result from extern decl: {libc_decl_res!r}"
            )
            libc_decl_uri = libc_decl_res[0].get("uri", "")
            assert (
                libc_decl_uri.endswith("/stdio.h")
                or libc_decl_uri.endswith("\\stdio.h")
                or libc_decl_uri.endswith("/_stdio.h")
                or libc_decl_uri.endswith("\\_stdio.h")
            ), (
                f"expected fopen extern decl to jump straight to the stdio declaration header: {libc_decl_res!r}"
            )

            libc_call_line, libc_call_char = position_of(libc_text, "fopen(\"/tmp/demo.txt\"")
            libc_call_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(libc_doc)},
                    "position": {"line": libc_call_line, "character": libc_call_char},
                },
            )
            assert isinstance(libc_call_res, list) and libc_call_res, (
                f"expected extern declaration result from call site: {libc_call_res!r}"
            )
            libc_call_target = libc_call_res[0]
            assert libc_call_target.get("uri") == to_uri(libc_doc), (
                f"expected fopen call site to jump to extern declaration first: {libc_call_res!r}"
            )
            libc_call_start = libc_call_target.get("range", {}).get("start", {})
            assert libc_call_start.get("line") == 0, (
                f"expected fopen call site to land on extern declaration line 1: {libc_call_res!r}"
            )
        finally:
            client.close()

    print("PASS: mlangd-mla C extern definition transcript (extern fn -> C source)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
