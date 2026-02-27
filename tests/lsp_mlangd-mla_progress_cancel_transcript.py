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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_progress_") as td:
        root = Path(td)
        file_path = root / "progress_case.mla"
        uri = to_uri(file_path)
        text = (
            "fn helper_a() -> i32 { return 1; }\n"
            "fn helper_b() -> i32 { return 2; }\n"
            "fn main() -> i32 { return helper_a() + helper_b(); }\n"
        )
        file_path.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            caps = init.get("capabilities", {})
            window = caps.get("window", {})
            assert window.get("workDoneProgress") is True, f"missing workDoneProgress capability: {caps!r}"
            client.notify("initialized", {})

            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": text,
                    }
                },
            )

            ws_result, notes = request_with_notifications(
                client,
                4001,
                "workspace/symbol",
                {"query": "helper"},
            )
            assert isinstance(ws_result, list), f"workspace/symbol should return list: {ws_result!r}"
            progress = [n for n in notes if n.get("method") == "$/progress"]
            assert progress, f"expected $/progress notifications while handling request: {notes!r}"
            kinds = [p.get("params", {}).get("value", {}).get("kind") for p in progress]
            assert "begin" in kinds and "end" in kinds, f"expected begin/end progress notifications: {progress!r}"

            client.notify("$/progress", {"token": 77, "value": {"kind": "report", "message": "client ping"}})
            client.notify("$/cancelRequest", {"id": 424242})

            hover = client.request(
                "textDocument/hover",
                {
                    "textDocument": {"uri": uri},
                    "position": {"line": 2, "character": 27},
                },
            )
            assert isinstance(hover, dict), f"hover should return object after hooks: {hover!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla progress + cancel transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
