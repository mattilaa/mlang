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
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_member_def_") as td:
        root = Path(td)
        doc = root / "member_def.mla"
        text = (
            "struct Counter {\n"
            "  var value: i32;\n"
            "  pub fn add(self: Counter, amount: i32) -> Counter {\n"
            "    return Counter { value: self.value + amount };\n"
            "  }\n"
            "};\n"
        )
        doc.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})

            open_doc(client, doc, text)

            line, char = position_of(text, "self.value")
            # Place cursor on member token ("value"), not object token ("self")
            char += len("self.")
            res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": line, "character": char},
                },
            )
            assert isinstance(res, list) and res, f"expected non-empty definition result: {res!r}"

            field_line, field_char = position_of(text, "var value: i32;")
            start = res[0].get("range", {}).get("start", {})
            assert res[0].get("uri") == to_uri(doc), f"definition uri mismatch: {res!r}"
            assert start.get("line") == field_line, f"definition line mismatch: {res!r}"
            assert start.get("character") == field_char + len("var "), f"definition character mismatch: {res!r}"

            amount_line, amount_char = position_of(text, "+ amount")
            amount_char += len("+ ")
            amount_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": amount_line, "character": amount_char},
                },
            )
            assert isinstance(amount_res, list) and amount_res, f"expected amount definition: {amount_res!r}"
            amount_param_line, amount_param_char = position_of(text, "amount: i32")
            amount_start = amount_res[0].get("range", {}).get("start", {})
            assert amount_start.get("line") == amount_param_line, f"amount line mismatch: {amount_res!r}"
            assert amount_start.get("character") == amount_param_char, f"amount character mismatch: {amount_res!r}"
        finally:
            client.close()

    print("PASS: mlangd_mla member definition transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
