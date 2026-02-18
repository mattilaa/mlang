#!/usr/bin/env python3
import argparse
import json
import subprocess
import tempfile
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
    last_nl = text.rfind("\n", 0, start)
    ch = start if last_nl < 0 else start - last_nl - 1
    return line, ch


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
            low = line.lower()
            if low.startswith(b"content-length:"):
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
                raise AssertionError("mlangd closed stdout while waiting for response")
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="build/mlangd")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_lsp_it_") as td:
        root = Path(td)

        impl_file = root / "impl.mla"
        refs_util = root / "lib" / "util_refs.mla"
        refs_main = root / "refs_main.mla"
        rename_file = root / "rename_case.mla"
        imports_file = root / "imports_case.mla"
        refs_util.parent.mkdir(parents=True, exist_ok=True)

        impl_text = (
            "struct Base {\n"
            "  fn run(self: Base) -> i32 { return 1; }\n"
            "};\n"
            "struct Derived : Base {\n"
            "  fn run(self: Derived) -> i32 { return 2; }\n"
            "};\n"
        )
        refs_util_text = "fn util_ref() -> i32 { return 9; }\n"
        refs_main_text = (
            "use lib::util_refs::util_ref;\n"
            "fn main() -> i32 {\n"
            "  let a: i32 = util_ref();\n"
            "  let b: i32 = util_ref();\n"
            "  return a + b;\n"
            "}\n"
        )
        rename_text = (
            "fn main() -> i32 {\n"
            "  let alpha: i32 = 1;\n"
            "  let beta: i32 = alpha + 1;\n"
            "  return beta;\n"
            "}\n"
        )
        imports_text = (
            "use z::z;\n"
            "use a::a;\n"
            "use a::a;\n"
            "fn main() -> i32 { return 0; }\n"
        )

        impl_file.write_text(impl_text)
        refs_util.write_text(refs_util_text)
        refs_main.write_text(refs_main_text)
        rename_file.write_text(rename_text)
        imports_file.write_text(imports_text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {
                    "processId": None,
                    "rootUri": to_uri(root),
                    "capabilities": {},
                },
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})

            for path, text in [
                (impl_file, impl_text),
                (refs_util, refs_util_text),
                (refs_main, refs_main_text),
                (rename_file, rename_text),
                (imports_file, imports_text),
            ]:
                client.notify(
                    "textDocument/didOpen",
                    {
                        "textDocument": {
                            "uri": to_uri(path),
                            "languageId": "mlang",
                            "version": 1,
                            "text": text,
                        }
                    },
                )

            impl_line, impl_char = position_of(impl_text, "run(self: Base)")
            impl_res = client.request(
                "textDocument/implementation",
                {
                    "textDocument": {"uri": to_uri(impl_file)},
                    "position": {"line": impl_line, "character": impl_char},
                },
            )
            assert isinstance(impl_res, list) and impl_res, "implementation result is empty"
            derived_line, _ = position_of(impl_text, "run(self: Derived)")
            assert any(
                item.get("uri") == to_uri(impl_file)
                and item.get("range", {}).get("start", {}).get("line") == derived_line
                for item in impl_res
            ), f"expected Derived::run implementation in {impl_res!r}"

            refs_line, refs_char = position_of(refs_main_text, "util_ref();", nth=1)
            refs_res = client.request(
                "textDocument/references",
                {
                    "textDocument": {"uri": to_uri(refs_main)},
                    "position": {"line": refs_line, "character": refs_char},
                    "context": {"includeDeclaration": True},
                },
            )
            assert isinstance(refs_res, list) and len(refs_res) >= 3, f"unexpected references: {refs_res!r}"
            assert any(item.get("uri") == to_uri(refs_util) for item in refs_res), "missing definition reference"

            ren_line, ren_char = position_of(rename_text, "alpha + 1")
            rename_res = client.request(
                "textDocument/rename",
                {
                    "textDocument": {"uri": to_uri(rename_file)},
                    "position": {"line": ren_line, "character": ren_char},
                    "newName": "beta",
                },
            )
            assert rename_res is None, f"unsafe rename should be blocked, got: {rename_res!r}"

            fmt_res = client.request(
                "textDocument/rangeFormatting",
                {
                    "textDocument": {"uri": to_uri(imports_file)},
                    "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}},
                    "options": {"tabSize": 2, "insertSpaces": True},
                },
            )
            assert isinstance(fmt_res, list), "rangeFormatting must return edit array"

            code_action_res = client.request(
                "textDocument/codeAction",
                {
                    "textDocument": {"uri": to_uri(imports_file)},
                    "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}},
                    "context": {"diagnostics": [], "only": ["source.organizeImports"]},
                },
            )
            assert isinstance(code_action_res, list) and code_action_res, "expected organizeImports code action"
            organize = next(
                (a for a in code_action_res if a.get("kind") == "source.organizeImports"),
                None,
            )
            assert organize is not None, f"missing source.organizeImports: {code_action_res!r}"
            edits = (
                organize.get("edit", {})
                .get("changes", {})
                .get(to_uri(imports_file), [])
            )
            assert edits, f"organizeImports returned no edits: {organize!r}"
            new_text = edits[0].get("newText", "")
            assert new_text == "use a::a;\nuse z::z;\n", f"unexpected organizeImports text: {new_text!r}"

            print("LSP integration transcript checks passed.")
        finally:
            client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
