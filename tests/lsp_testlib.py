#!/usr/bin/env python3
import json
import subprocess
from pathlib import Path


def to_uri(path: Path) -> str:
    return path.resolve().as_uri()


def position_of(text: str, needle: str, nth: int = 1) -> tuple[int, int]:
    start = -1
    cur = 0
    for _ in range(nth):
        start = text.find(needle, cur)
        if start < 0:
            raise AssertionError(f"needle not found: {needle!r}")
        cur = start + len(needle)
    line = text.count("\n", 0, start)
    line_start = text.rfind("\n", 0, start)
    char = start if line_start < 0 else start - line_start - 1
    return line, char


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
                raise AssertionError("mlangd-mla closed stdout while waiting for response")
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
