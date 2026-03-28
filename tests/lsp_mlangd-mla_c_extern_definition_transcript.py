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
        printf_doc = root / "c_lib_usage.mla"
        printf_c_src = root / "c_wrapper.c"
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
        printf_text = (
            "extern fn printf(format: str8, value: i32) -> i32;\n"
            "\n"
            "fn main() -> i32 {\n"
            "  return printf(\"n=%d\\n\", 7);\n"
            "}\n"
        )
        printf_c_text = (
            "#include <stdio.h>\n"
            "\n"
            "int printf(const char* format, int value) {\n"
            "  return fprintf(stdout, format, value);\n"
            "}\n"
        )
        doc.write_text(text)
        c_src.write_text(c_text)
        printf_doc.write_text(printf_text)
        printf_c_src.write_text(printf_c_text)

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

            open_doc(client, printf_doc, printf_text)

            printf_decl_line, printf_decl_char = position_of(printf_text, "printf(format:")
            printf_decl_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(printf_doc)},
                    "position": {"line": printf_decl_line, "character": printf_decl_char},
                },
            )
            assert isinstance(printf_decl_res, list) and printf_decl_res, (
                f"expected printf C definition from declaration: {printf_decl_res!r}"
            )
            printf_decl_target = printf_decl_res[0]
            assert printf_decl_target.get("uri") == to_uri(printf_c_src), (
                f"expected printf declaration jump to c_wrapper.c: {printf_decl_res!r}"
            )
            printf_decl_start = printf_decl_target.get("range", {}).get("start", {})
            assert printf_decl_start.get("line") == 2, (
                f"expected printf wrapper on line 3 in C source: {printf_decl_res!r}"
            )

            printf_call_line, printf_call_char = position_of(printf_text, "printf(\"n=%d\\n\", 7)")
            printf_call_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(printf_doc)},
                    "position": {"line": printf_call_line, "character": printf_call_char},
                },
            )
            assert isinstance(printf_call_res, list) and printf_call_res, (
                f"expected printf C definition from call site: {printf_call_res!r}"
            )
            printf_call_target = printf_call_res[0]
            assert printf_call_target.get("uri") == to_uri(printf_c_src), (
                f"expected printf call-site jump to c_wrapper.c: {printf_call_res!r}"
            )
            printf_call_start = printf_call_target.get("range", {}).get("start", {})
            assert printf_call_start.get("line") == 2, (
                f"expected printf call-site wrapper on line 3 in C source: {printf_call_res!r}"
            )
        finally:
            client.close()

    print("PASS: mlangd-mla C extern definition transcript (extern fn -> C source)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
