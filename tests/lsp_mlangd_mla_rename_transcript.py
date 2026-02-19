#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def to_uri(path: Path) -> str:
    return path.resolve().as_uri()


class JsonRpcClient:
    def __init__(self, argv: list[str]):
        self.proc = subprocess.Popen(
            argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._next_id = 1

    def _write(self, payload: dict) -> None:
        data = json.dumps(payload).encode("utf-8")
        header = f"Content-Length: {len(data)}\r\n\r\n".encode("ascii")
        assert self.proc.stdin is not None
        self.proc.stdin.write(header)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def _read(self) -> dict | None:
        assert self.proc.stdout is not None
        content_length = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                return None
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b"content-length:"):
                content_length = int(line.split(b":", 1)[1].strip())
        if content_length is None:
            return None
        body = self.proc.stdout.read(content_length)
        if not body:
            return None
        return json.loads(body.decode("utf-8"))

    def request(self, method: str, params: dict) -> object:
        req_id = self._next_id
        self._next_id += 1
        self._write({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
        while True:
            msg = self._read()
            if msg is None:
                raise AssertionError("mlangd_mla closed stdout while waiting for response")
            if msg.get("id") == req_id:
                if "error" in msg:
                    raise AssertionError(f"LSP error for {method}: {msg['error']}")
                return msg.get("result")

    def notify(self, method: str, params: dict) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def close(self) -> None:
        try:
            self.request("shutdown", {})
        finally:
            self.notify("exit", {})
            if self.proc.stdin:
                self.proc.stdin.close()
            self.proc.wait(timeout=5)


def position_of(text: str, needle: str, nth: int = 1) -> tuple[int, int]:
    start = 0
    for _ in range(nth):
        idx = text.find(needle, start)
        if idx < 0:
            raise AssertionError(f"needle not found: {needle!r}")
        start = idx + 1
    line = text.count("\n", 0, idx)
    line_start = text.rfind("\n", 0, idx)
    char = idx if line_start < 0 else idx - line_start - 1
    return line, char


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_rename_") as td:
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

    print("PASS: mlangd_mla rename transcript (documentChanges + cross-document edits)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
