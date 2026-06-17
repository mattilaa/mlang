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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_comp_docs_") as td:
        root = Path(td)
        doc = root / "completion_docs.mla"
        text = (
            "fn main() -> i32 {\n"
            "  if\n"
            "  names\n"
            "  i32\n"
            "  lis\n"
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
            open_doc(client, doc, text)

            int_line, int_char = position_of(text, "  i32")
            int_char += len("  i32")
            res_int = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": int_line, "character": int_char},
                },
            )
            int_item = find_item(res_int, "i32")
            assert isinstance(int_item.get("detail"), str) and int_item.get("detail"), (
                f"i32 completion should include detail: {int_item!r}"
            )
            int_doc = int_item.get("documentation", {})
            assert isinstance(int_doc, dict) and isinstance(int_doc.get("value"), str), (
                f"i32 completion should include documentation: {int_item!r}"
            )
            assert "32-bit" in int_doc.get("value", ""), (
                f"i32 documentation should describe the type: {int_item!r}"
            )

            list_line, list_char = position_of(text, "  lis")
            list_char += len("  lis")
            res_list = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": list_line, "character": list_char},
                },
            )
            list_item = find_item(res_list, "list")
            assert list_item.get("kind") == 22, (
                f"list completion should be a type/struct kind: {list_item!r}"
            )
            assert isinstance(list_item.get("detail"), str) and list_item.get("detail"), (
                f"list completion should include detail: {list_item!r}"
            )
            list_doc = list_item.get("documentation", {})
            assert isinstance(list_doc, dict) and isinstance(list_doc.get("value"), str), (
                f"list completion should include documentation: {list_item!r}"
            )
            assert "list<T>" in list_doc.get("value", ""), (
                f"list documentation should describe the generic type: {list_item!r}"
            )
            assert "push(value)" in list_doc.get("value", ""), (
                f"list documentation should mention common methods: {list_item!r}"
            )

            if_line, if_char = position_of(text, "if\n")
            if_char += len("if")
            res_if = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": if_line, "character": if_char},
                },
            )
            if_item = find_item(res_if, "if")
            assert isinstance(if_item.get("detail"), str) and if_item.get("detail"), (
                f"if completion should include detail: {if_item!r}"
            )
            pdoc = if_item.get("documentation", {})
            assert isinstance(pdoc, dict) and isinstance(pdoc.get("value"), str), (
                f"if completion should include documentation: {if_item!r}"
            )
            assert "Conditional" in pdoc.get("value", ""), (
                f"if documentation should describe behavior: {if_item!r}"
            )

            ns_line, ns_char = position_of(text, "  names")
            ns_char += len("  names")
            res_ns = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": ns_line, "character": ns_char},
                },
            )
            ns_item = find_item(res_ns, "namespace")
            assert ns_item.get("kind") == 14, (
                f"namespace completion should be a keyword kind: {ns_item!r}"
            )
            ns_doc = ns_item.get("documentation", {})
            assert isinstance(ns_doc, dict) and isinstance(ns_doc.get("value"), str), (
                f"namespace completion should include documentation: {ns_item!r}"
            )
            assert "qualified namespace block" in ns_doc.get("value", ""), (
                f"namespace documentation should describe namespace blocks: {ns_item!r}"
            )
        finally:
            client.close()

    print("PASS: mlangd-mla completion transcript includes detail + documentation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
