#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
import time
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


def code_action_params(uri: str) -> dict:
    return {
        "textDocument": {"uri": uri},
        "range": {"start": {"line": 0, "character": 0}, "end": {"line": 0, "character": 0}},
        "context": {"diagnostics": [], "only": ["source.organizeImports"]},
    }


def require_organize_imports_result(result: object, uri: str, expected_new_text: str) -> None:
    assert isinstance(result, list) and result, f"expected non-empty codeAction list: {result!r}"
    organize = next((a for a in result if a.get("kind") == "source.organizeImports"), None)
    assert organize is not None, f"missing source.organizeImports action: {result!r}"
    edits = organize.get("edit", {}).get("changes", {}).get(uri, [])
    assert edits, f"organizeImports has no edits for uri={uri}: {organize!r}"
    assert edits[0].get("newText") == expected_new_text, (
        f"unexpected organizeImports newText: {edits[0].get('newText')!r}, "
        f"expected {expected_new_text!r}"
    )


def wait_for_expected_organize_imports(
    client: JsonRpcClient, uri: str, expected_new_text: str, timeout_s: float = 1.0
) -> None:
    deadline = time.monotonic() + timeout_s
    last_result = None
    while time.monotonic() < deadline:
        result = client.request("textDocument/codeAction", code_action_params(uri))
        last_result = result
        try:
            require_organize_imports_result(result, uri, expected_new_text)
            return
        except AssertionError:
            time.sleep(0.02)
    require_organize_imports_result(last_result, uri, expected_new_text)


def wait_for_empty_code_action(
    client: JsonRpcClient, uri: str, timeout_s: float = 1.0
) -> None:
    deadline = time.monotonic() + timeout_s
    last_result = None
    while time.monotonic() < deadline:
        result = client.request("textDocument/codeAction", code_action_params(uri))
        last_result = result
        if result == []:
            return
        time.sleep(0.02)
    assert last_result == [], f"expected [] after didClose, got {last_result!r}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_codeaction_") as td:
        root = Path(td)
        file_path = root / "imports_case.mla"
        uri = to_uri(file_path)

        text_v1 = (
            "use z::z;\n"
            "use a::a;\n"
            "use a::a;\n"
            "fn main() -> i32 { return 0; }\n"
        )
        text_v2 = (
            "use zz::z;\n"
            "use aa::a;\n"
            "use aa::a;\n"
            "fn main() -> i32 { return 0; }\n"
        )

        file_path.write_text(text_v1)

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
                        "uri": uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": text_v1,
                    }
                },
            )
            wait_for_expected_organize_imports(client, uri, "use a::a;\nuse z::z;\n")

            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": text_v2}],
                },
            )
            wait_for_expected_organize_imports(client, uri, "use aa::a;\nuse zz::z;\n")

            client.notify(
                "textDocument/didClose",
                {"textDocument": {"uri": uri}},
            )
            wait_for_empty_code_action(client, uri)
        finally:
            client.close()

    print("PASS: mlangd_mla codeAction organizeImports transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
