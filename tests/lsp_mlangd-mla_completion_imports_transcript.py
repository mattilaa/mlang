#!/usr/bin/env python3
"""Bound completion latency for import-heavy files and keyword documentation."""
import argparse
import threading
import time
from pathlib import Path

from lsp_testlib import JsonRpcClient, position_of, to_uri


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    path = root / "examples/package_manager_delay_audio/src/main.mla"
    text = path.read_text()
    client = JsonRpcClient([str(Path(args.mlangd).resolve()), "--stdio"])
    # A regression must fail instead of leaving the transcript runner hung.
    watchdog = threading.Timer(20, client.proc.kill)
    watchdog.start()
    try:
        client.request("initialize", {"processId": None, "rootUri": to_uri(root), "capabilities": {}})
        client.notify("initialized", {})
        client.notify("textDocument/didOpen", {"textDocument": {
            "uri": to_uri(path), "languageId": "mlang", "version": 1, "text": text,
        }})
        for needle, prefix, expected in [
            ("mod std::argparser;", "mod", "mod"),
            ("use dsp::delay::DelayFrame;", "use", "use"),
            ("mod std::argparser;", "mo", "mod"),
            ("use dsp::delay::DelayFrame;", "us", "use"),
        ]:
            line, char = position_of(text, needle)
            started = time.monotonic()
            items = client.request("textDocument/completion", {
                "textDocument": {"uri": to_uri(path)},
                "position": {"line": line, "character": char + len(prefix)},
            })
            elapsed = time.monotonic() - started
            assert elapsed < 3, f"{prefix!r} completion took {elapsed:.2f}s"
            item = next(item for item in items if item["label"] == expected)
            assert item["kind"] == 14
            assert item["documentation"]["value"], item
        # Editing invalidates semantic caches. Exercise incomplete imports at
        # the end of the same large document, as when typing a new declaration.
        for version, prefix, expected in [(2, "mo", "mod"), (3, "us", "use")]:
            edited = text + "\n" + prefix
            client.notify("textDocument/didChange", {
                "textDocument": {"uri": to_uri(path), "version": version},
                "contentChanges": [{"text": edited}],
            })
            started = time.monotonic()
            items = client.request("textDocument/completion", {
                "textDocument": {"uri": to_uri(path)},
                "position": {"line": edited.count("\n"), "character": len(prefix)},
            })
            assert time.monotonic() - started < 3, f"edited {prefix!r} completion stalled"
            assert any(item["label"] == expected for item in items), items
        client.close()
    finally:
        watchdog.cancel()
        if client.proc.poll() is None:
            client.proc.kill()
        client.proc.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
