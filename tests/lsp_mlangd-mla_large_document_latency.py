#!/usr/bin/env python3
"""Keep didChange and ordinary completion responsive in a large buffer."""
import argparse
import tempfile
import threading
import time
from pathlib import Path

from lsp_testlib import JsonRpcClient, to_uri


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_large_") as td:
        path = Path(td) / "large.mla"
        text = "fn main() -> i32 {\n  let citrus: i32 = 1;\n  ci\n  return 0;\n}\n"
        text += "// padding keeps this document above the fast-path threshold\n" * 1300
        assert len(text) > 65536
        path.write_text(text)

        client = JsonRpcClient([str(Path(args.mlangd).resolve()), "--stdio"])
        watchdog = threading.Timer(10, client.proc.kill)
        watchdog.start()
        try:
            client.request("initialize", {
                "processId": None, "rootUri": to_uri(path.parent), "capabilities": {},
            })
            client.notify("initialized", {})
            client.notify("textDocument/didOpen", {"textDocument": {
                "uri": to_uri(path), "languageId": "mlang", "version": 1, "text": text,
            }})
            client.read_until_notification("textDocument/publishDiagnostics")

            changed = text + "\nuse tui::"
            started = time.monotonic()
            client.notify("textDocument/didChange", {"textDocument": {
                "uri": to_uri(path), "version": 2,
            }, "contentChanges": [{"text": changed}]})
            client.read_until_notification("textDocument/publishDiagnostics")
            assert time.monotonic() - started < 1, "didChange stalled on large document"

            started = time.monotonic()
            items = client.request("textDocument/completion", {
                "textDocument": {"uri": to_uri(path)},
                "position": {"line": changed.count("\n"), "character": len("use tui::")},
            })
            assert time.monotonic() - started < 1, "completion stalled on large document"
            assert any(item.get("label") == "table" for item in items), items

            nested = text + "\nuse tui::table::"
            client.notify("textDocument/didChange", {"textDocument": {
                "uri": to_uri(path), "version": 3,
            }, "contentChanges": [{"text": nested}]})
            client.read_until_notification("textDocument/publishDiagnostics")
            items = client.request("textDocument/completion", {
                "textDocument": {"uri": to_uri(path)},
                "position": {"line": nested.count("\n"), "character": len("use tui::table::")},
            })
            assert any(item.get("label") == "Table" for item in items), items
            client.close()
        finally:
            watchdog.cancel()
            if client.proc.poll() is None:
                client.proc.kill()
            client.proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
