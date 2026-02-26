#!/usr/bin/env python3
import json
import os
import subprocess
from datetime import datetime
from pathlib import Path


_DEBUG_ENV = os.environ.get("MLANG_TEST_DEBUG", "0").strip().lower()
_DEBUG_ENABLED = _DEBUG_ENV in {"1", "true", "yes", "on", "debug"}


def _debug(message: str) -> None:
    if not _DEBUG_ENABLED:
        return
    ts = datetime.now().strftime("%d/%m/%H/%M/%S")
    print(f"{ts} [DEBUG] {message}", flush=True)


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
        _debug(f"spawned process: {' '.join(argv)}")

    def _write(self, payload: dict) -> None:
        data = json.dumps(payload).encode("utf-8")
        header = f"Content-Length: {len(data)}\r\n\r\n".encode("ascii")
        assert self.proc.stdin is not None
        self.proc.stdin.write(header)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        method = payload.get("method", "<unknown>")
        req_id = payload.get("id")
        if req_id is None:
            _debug(f"-> notify {method}")
        else:
            _debug(f"-> request id={req_id} method={method}")

    def _read(self) -> dict | None:
        assert self.proc.stdout is not None
        content_length = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                _debug("<- EOF on stdout")
                return None
            line = line.strip()
            if not line:
                break
            if line.lower().startswith(b"content-length:"):
                content_length = int(line.split(b":", 1)[1].strip())
        if content_length is None:
            _debug("<- missing Content-Length")
            return None
        body = self.proc.stdout.read(content_length)
        if not body:
            _debug("<- empty payload body")
            return None
        msg = json.loads(body.decode("utf-8"))
        if "method" in msg:
            _debug(f"<- notify {msg.get('method')}")
        else:
            _debug(f"<- response id={msg.get('id')}")
        return msg

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
                    _debug(f"request id={req_id} error={msg['error']}")
                    raise AssertionError(f"LSP error for {method}: {msg['error']}")
                _debug(f"request id={req_id} completed")
                return msg.get("result")

    def notify(self, method: str, params: dict) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def close(self) -> None:
        _debug("closing client")
        try:
            self.request("shutdown", {})
        finally:
            self.notify("exit", {})
            if self.proc.stdin:
                self.proc.stdin.close()
            self.proc.wait(timeout=5)
            _debug("client closed")
