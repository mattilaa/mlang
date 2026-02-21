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
            "struct Session {\n"
            "  var id: i32;\n"
            "};\n"
            "\n"
            "fn run(s: Session) -> i32 {\n"
            "  let Sess: Session = s;\n"
            "  let x: Session = Ses;\n"
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
        finally:
            client.close()

    print("PASS: mlangd-mla completion transcript includes symbol details")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
