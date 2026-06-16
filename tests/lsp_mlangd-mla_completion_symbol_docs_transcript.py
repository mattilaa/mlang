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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_comp_sym_docs_") as td:
        root = Path(td)
        doc = root / "completion_symbol_docs.mla"
        text = (
            "/**\n"
            " * @brief Distance in meters.\n"
            " * Used for physics and map measurements.\n"
            " */\n"
            "alias Distance = f32;\n"
            "\n"
            "struct Session {\n"
            "  var id: i32;\n"
            "};\n"
            "\n"
            "fn run(s: Session) -> i32 {\n"
            "  let Sess: Session = s;\n"
            "  let x: Session = Ses;\n"
            "  let d: Distance = Dis;\n"
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

            line, char = position_of(text, "Ses;")
            char += len("Ses")
            res = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": line, "character": char},
                },
            )

            session_item = find_item(res, "Session")
            session_detail = session_item.get("detail", "")
            assert isinstance(session_detail, str) and session_detail, (
                f"Session completion should include detail: {session_item!r}"
            )
            assert session_detail != "symbol", (
                f"Session detail must not be generic 'symbol': {session_item!r}"
            )

            sess_item = find_item(res, "Sess")
            sess_detail = sess_item.get("detail", "")
            assert isinstance(sess_detail, str) and sess_detail, (
                f"Sess completion should include detail: {sess_item!r}"
            )
            assert sess_detail != "symbol", (
                f"Sess detail must not be generic 'symbol': {sess_item!r}"
            )

            alias_line, alias_char = position_of(text, "Dis;")
            alias_char += len("Dis")
            alias_res = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": alias_line, "character": alias_char},
                },
            )
            distance_item = find_item(alias_res, "Distance")
            distance_detail = distance_item.get("detail", "")
            assert isinstance(distance_detail, str) and "f32" in distance_detail, (
                f"Distance alias completion should include type detail: {distance_item!r}"
            )
            distance_doc = distance_item.get("documentation", {})
            assert isinstance(distance_doc, dict), (
                f"Distance completion should include documentation object: {distance_item!r}"
            )
            distance_doc_value = distance_doc.get("value", "")
            assert "Distance in meters." in distance_doc_value, (
                f"Distance documentation should include Doxygen brief text: {distance_item!r}"
            )
            assert "Used for physics" in distance_doc_value, (
                f"Distance documentation should include full Doxygen body: {distance_item!r}"
            )
        finally:
            client.close()

    print("PASS: mlangd-mla completion transcript includes symbol details and alias docs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
