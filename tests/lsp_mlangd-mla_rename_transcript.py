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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_rename_") as td:
        root = Path(td)
        defs_file = root / "rename_defs.mla"
        use_file = root / "rename_use.mla"
        defs_uri = to_uri(defs_file)
        use_uri = to_uri(use_file)

        defs_text = (
            "fn alpha() -> i32 { return 1; }\n"
            "fn local_use() -> i32 { return alpha(); }\n"
        )
        use_text = (
            "mod rename_defs;\n"
            "fn remote_use() -> i32 { return alpha(); }\n"
        )

        defs_file.write_text(defs_text)
        use_file.write_text(use_text)

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
                        "uri": defs_uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": defs_text,
                    }
                },
            )
            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": use_uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": use_text,
                    }
                },
            )

            ren_line, ren_char = position_of(defs_text, "alpha();")
            rename_res = client.request(
                "textDocument/rename",
                {
                    "textDocument": {"uri": defs_uri},
                    "position": {"line": ren_line, "character": ren_char},
                    "newName": "gamma",
                },
            )
            assert isinstance(rename_res, dict), f"rename result must be object: {rename_res!r}"
            assert "changes" not in rename_res, f"expected documentChanges form, got legacy changes: {rename_res!r}"

            doc_changes = rename_res.get("documentChanges")
            assert isinstance(doc_changes, list) and doc_changes, f"documentChanges missing/empty: {rename_res!r}"

            uris_seen: set[str] = set()
            for change in doc_changes:
                assert isinstance(change, dict), f"documentChange must be object: {change!r}"
                text_doc = change.get("textDocument")
                edits = change.get("edits")
                assert isinstance(text_doc, dict), f"missing textDocument: {change!r}"
                assert isinstance(edits, list) and edits, f"missing edits list: {change!r}"
                uri = text_doc.get("uri")
                assert isinstance(uri, str) and uri, f"missing textDocument.uri: {change!r}"
                uris_seen.add(uri)
                for edit in edits:
                    assert isinstance(edit, dict), f"edit must be object: {edit!r}"
                    assert edit.get("newText") == "gamma", f"unexpected rename newText: {edit!r}"
                    rng = edit.get("range")
                    assert isinstance(rng, dict), f"edit range missing: {edit!r}"

            assert defs_uri in uris_seen, f"definition doc edit missing: uris={sorted(uris_seen)!r}"
            assert use_uri in uris_seen, f"cross-document edit missing: uris={sorted(uris_seen)!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla rename transcript (documentChanges + cross-document edits)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
