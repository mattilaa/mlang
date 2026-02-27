#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def request_with_notifications(client: JsonRpcClient, req_id: int, method: str, params: dict) -> tuple[object, list[dict]]:
    client._write({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
    notes: list[dict] = []
    while True:
        msg = client._read()
        if msg is None:
            raise AssertionError("mlangd-mla closed stdout while waiting for response")
        if msg.get("id") == req_id:
            if "error" in msg:
                raise AssertionError(f"LSP error for {method}: {msg['error']}")
            return msg.get("result"), notes
        if msg.get("method"):
            notes.append(msg)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_push_diag_") as td:
        root = Path(td)
        file_path = root / "push_diag_case.mla"
        uri = to_uri(file_path)
        text_ok = "fn ok() -> i32 { return 1; }\n"
        text_bad = "fn bad( -> i32 { return 1; }\n"
        file_path.write_text(text_ok)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            sync = init.get("capabilities", {}).get("textDocumentSync", {})
            assert sync.get("save") is True, f"textDocumentSync.save capability missing: {sync!r}"
            client.notify("initialized", {})

            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": text_ok,
                    }
                },
            )
            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": text_bad}],
                },
            )
            client.notify("textDocument/didSave", {"textDocument": {"uri": uri}})

            _hover, notes = request_with_notifications(
                client,
                9011,
                "textDocument/hover",
                {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 3}},
            )
            pushes = [n for n in notes if n.get("method") == "textDocument/publishDiagnostics"]
            assert pushes, f"expected publishDiagnostics notifications: {notes!r}"
            for n in pushes:
                params = n.get("params", {})
                if params.get("uri") != uri:
                    continue
                diags = params.get("diagnostics")
                assert isinstance(diags, list), f"diagnostics must be list: {n!r}"
                break
            else:
                raise AssertionError(f"publishDiagnostics for target uri missing: {pushes!r}")
        finally:
            client.close()

    print("PASS: mlangd-mla publish diagnostics transcript (didOpen/didChange/didSave)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
