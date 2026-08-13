#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def find_item(items: object, label: str) -> dict:
    assert isinstance(items, list), f"completion should return list: {items!r}"
    for it in items:
        if isinstance(it, dict) and it.get("label") == label:
            return it
    raise AssertionError(f"completion item not found: {label!r} in {items!r}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_result_completion_") as td:
        root = Path(td)
        doc = root / "result_completion.mla"
        text = (
            "fn main() -> i32 {\n"
            "  let r: result<i32, str8> = Ok<i32, str8>(7);\n"
            "  r.\n"
            "  return 0;\n"
            "}\n"
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
            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": to_uri(doc),
                        "languageId": "mlang",
                        "version": 1,
                        "text": text,
                    }
                },
            )

            line, char = position_of(text, "r.")
            char += len("r.")
            res = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": line, "character": char},
                },
            )
            assert isinstance(res, list) and res, f"result member completion should return items: {res!r}"

            unwrap_item = find_item(res, "unwrap")
            is_err_item = find_item(res, "is_err")
            assert isinstance(unwrap_item.get("detail"), str) and unwrap_item.get("detail"), (
                f"unwrap completion should include detail: {unwrap_item!r}"
            )
            assert isinstance(is_err_item.get("detail"), str) and is_err_item.get("detail"), (
                f"is_err completion should include detail: {is_err_item!r}"
            )
            unwrap_doc = unwrap_item.get("documentation", {})
            is_err_doc = is_err_item.get("documentation", {})
            assert isinstance(unwrap_doc, dict) and isinstance(unwrap_doc.get("value"), str), (
                f"unwrap completion should include documentation: {unwrap_item!r}"
            )
            assert isinstance(is_err_doc, dict) and isinstance(is_err_doc.get("value"), str), (
                f"is_err completion should include documentation: {is_err_item!r}"
            )
            assert "panic" in unwrap_doc.get("value", ""), (
                f"unwrap documentation should mention panic behavior: {unwrap_item!r}"
            )
            assert "Err" in is_err_doc.get("value", ""), (
                f"is_err documentation should describe Err behavior: {is_err_item!r}"
            )
        finally:
            client.close()

    print("PASS: mlangd-mla completion transcript includes cached result methods")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
