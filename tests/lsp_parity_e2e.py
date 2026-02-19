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


def write_bulk_workspace(root: Path, n: int) -> None:
    bulk = root / "bulk"
    bulk.mkdir(parents=True, exist_ok=True)
    for i in range(n):
        p = bulk / f"f{i}.mla"
        p.write_text(f"fn bulk_fn_{i}() -> i32 {{ return {i}; }}\n")


def open_doc(client: JsonRpcClient, path: Path, text: str, version: int = 1) -> None:
    client.notify(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": to_uri(path),
                "languageId": "mlang",
                "version": version,
                "text": text,
            }
        },
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="build/mlangd")
    ap.add_argument("--bulk-files", type=int, default=220)
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_lsp_parity_") as td:
        root = Path(td)
        write_bulk_workspace(root, args.bulk_files)

        (root / "modules" / "lib").mkdir(parents=True, exist_ok=True)
        (root / "lib").mkdir(parents=True, exist_ok=True)

        manifest = root / "mlang.toml"
        manifest.write_text("[tool.mlang]\nmodule_paths = [\"modules\"]\n")

        impl_file = root / "impl.mla"
        refs_util = root / "lib" / "util_refs.mla"
        refs_main = root / "refs_main.mla"
        rename_file = root / "rename_case.mla"
        imports_file = root / "imports_case.mla"
        cfg_main = root / "app_cfg.mla"
        cfg_util = root / "modules" / "lib" / "util_cfg.mla"
        sig_file = root / "sig_case.mla"
        diag_file = root / "diag_case.mla"
        sem_file = root / "sem_case.mla"

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
            "  let alpha: i32 = util_ref();\n"
            "  let beta: i32 = alpha + util_ref();\n"
            "  return beta;\n"
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
        cfg_main_text = (
            "use lib::util_cfg::util_cfg;\n"
            "fn main() -> i32 { return util_cfg(); }\n"
        )
        cfg_util_text = "fn util_cfg() -> i32 { return 7; }\n"
        sig_text = (
            "fn helper(a: i32, b: i32) -> i32 { return a + b; }\n"
            "fn main() -> i32 {\n"
            "  return helper(1, 2);\n"
            "}\n"
        )
        diag_text = "fn main() -> i32 { let x: i32 = ; }\n"
        sem_text = "fn sem_fn(x: i32) -> i32 { return x; }\n"

        impl_file.write_text(impl_text)
        refs_util.write_text(refs_util_text)
        refs_main.write_text(refs_main_text)
        rename_file.write_text(rename_text)
        imports_file.write_text(imports_text)
        cfg_main.write_text(cfg_main_text)
        cfg_util.write_text(cfg_util_text)
        sig_file.write_text(sig_text)
        diag_file.write_text(diag_text)
        sem_file.write_text(sem_text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize result must be object"
            caps = init.get("capabilities", {})
            assert isinstance(caps, dict), "capabilities missing"
            client.notify("initialized", {})

            for path, text in [
                (impl_file, impl_text),
                (refs_util, refs_util_text),
                (refs_main, refs_main_text),
                (rename_file, rename_text),
                (imports_file, imports_text),
                (cfg_main, cfg_main_text),
                (sig_file, sig_text),
                (diag_file, diag_text),
                (sem_file, sem_text),
            ]:
                open_doc(client, path, text)

            # implementation
            impl_line, impl_char = position_of(impl_text, "run(self: Base)")
            impl_res = client.request(
                "textDocument/implementation",
                {"textDocument": {"uri": to_uri(impl_file)}, "position": {"line": impl_line, "character": impl_char}},
            )
            assert isinstance(impl_res, list) and impl_res, "implementation empty"

            # definition via module_paths
            cfg_line, cfg_char = position_of(cfg_main_text, "util_cfg();")
            cfg_def = client.request(
                "textDocument/definition",
                {"textDocument": {"uri": to_uri(cfg_main)}, "position": {"line": cfg_line, "character": cfg_char}},
            )
            assert isinstance(cfg_def, list) and cfg_def, "definition empty"
            assert any(item.get("uri") == to_uri(cfg_util) for item in cfg_def), "definition missed module_paths target"

            # references
            refs_line, refs_char = position_of(refs_main_text, "util_ref();", nth=1)
            refs_res = client.request(
                "textDocument/references",
                {
                    "textDocument": {"uri": to_uri(refs_main)},
                    "position": {"line": refs_line, "character": refs_char},
                    "context": {"includeDeclaration": True},
                },
            )
            assert isinstance(refs_res, list) and len(refs_res) >= 3, "references too small"

            # hover
            hover_line, hover_char = position_of(refs_main_text, "alpha +")
            hover_res = client.request(
                "textDocument/hover",
                {"textDocument": {"uri": to_uri(refs_main)}, "position": {"line": hover_line, "character": hover_char}},
            )
            assert isinstance(hover_res, dict) and hover_res.get("contents"), "hover missing"

            # documentHighlight
            hl_res = client.request(
                "textDocument/documentHighlight",
                {"textDocument": {"uri": to_uri(refs_main)}, "position": {"line": hover_line, "character": hover_char}},
            )
            assert isinstance(hl_res, list) and hl_res, "documentHighlight empty"

            # completion
            comp_line, comp_char = position_of(refs_main_text, "alpha +")
            comp_res = client.request(
                "textDocument/completion",
                {"textDocument": {"uri": to_uri(refs_main)}, "position": {"line": comp_line, "character": comp_char}},
            )
            assert isinstance(comp_res, list), "completion result not array"

            # signatureHelp
            sig_line, sig_char = position_of(sig_text, "helper(1, 2)")
            sig_res = client.request(
                "textDocument/signatureHelp",
                {"textDocument": {"uri": to_uri(sig_file)}, "position": {"line": sig_line, "character": sig_char + 9}},
            )
            assert isinstance(sig_res, dict) and sig_res.get("signatures"), "signatureHelp missing"

            # prepareRename + rename safe
            ren_line, ren_char = position_of(rename_text, "alpha + 1")
            prep_res = client.request(
                "textDocument/prepareRename",
                {"textDocument": {"uri": to_uri(rename_file)}, "position": {"line": ren_line, "character": ren_char}},
            )
            assert isinstance(prep_res, dict) and prep_res.get("range"), "prepareRename missing range"
            rename_safe = client.request(
                "textDocument/rename",
                {
                    "textDocument": {"uri": to_uri(rename_file)},
                    "position": {"line": ren_line, "character": ren_char},
                    "newName": "gamma",
                },
            )
            assert isinstance(rename_safe, dict), "rename safe should produce workspace edit"
            rename_blocked = client.request(
                "textDocument/rename",
                {
                    "textDocument": {"uri": to_uri(rename_file)},
                    "position": {"line": ren_line, "character": ren_char},
                    "newName": "beta",
                },
            )
            assert rename_blocked is None, "unsafe rename should be blocked"

            # documentSymbol
            doc_syms = client.request("textDocument/documentSymbol", {"textDocument": {"uri": to_uri(refs_main)}})
            assert isinstance(doc_syms, list) and doc_syms, "documentSymbol empty"

            # formatting + rangeFormatting
            fmt_res = client.request(
                "textDocument/formatting",
                {"textDocument": {"uri": to_uri(imports_file)}, "options": {"tabSize": 2, "insertSpaces": True}},
            )
            assert isinstance(fmt_res, list), "formatting must return array"
            rfmt_res = client.request(
                "textDocument/rangeFormatting",
                {
                    "textDocument": {"uri": to_uri(imports_file)},
                    "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}},
                    "options": {"tabSize": 2, "insertSpaces": True},
                },
            )
            assert isinstance(rfmt_res, list), "rangeFormatting must return array"

            # codeAction
            ca_res = client.request(
                "textDocument/codeAction",
                {
                    "textDocument": {"uri": to_uri(imports_file)},
                    "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}},
                    "context": {"diagnostics": [], "only": ["source.organizeImports"]},
                },
            )
            assert isinstance(ca_res, list) and ca_res, "codeAction empty"

            # diagnostic pull
            dg_res = client.request(
                "textDocument/diagnostic",
                {"textDocument": {"uri": to_uri(diag_file)}},
            )
            assert isinstance(dg_res, dict), "diagnostic pull must return object"
            assert isinstance(dg_res.get("items", []), list), "diagnostic items missing"

            # semantic tokens
            sem_tok = client.request(
                "textDocument/semanticTokens/full",
                {"textDocument": {"uri": to_uri(sem_file)}},
            )
            assert isinstance(sem_tok, dict), "semanticTokens/full must return object"
            assert isinstance(sem_tok.get("data", []), list), "semantic token data missing"

            # workspace symbol (large workspace)
            target_name = f"bulk_fn_{args.bulk_files - 1}"
            ws_res = client.request("workspace/symbol", {"query": target_name})
            assert isinstance(ws_res, list), "workspace/symbol must return array"
            assert any(item.get("name") == target_name for item in ws_res), "workspace/symbol missing bulk target"

            # didChange -> verify hover still works
            changed = refs_main_text.replace("alpha + util_ref()", "alpha + alpha")
            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": to_uri(refs_main), "version": 2},
                    "contentChanges": [{"text": changed}],
                },
            )
            hover_after = client.request(
                "textDocument/hover",
                {"textDocument": {"uri": to_uri(refs_main)}, "position": {"line": hover_line, "character": hover_char}},
            )
            assert isinstance(hover_after, dict), "hover after didChange missing"

            # didSave/didClose lifecycle
            client.notify("textDocument/didSave", {"textDocument": {"uri": to_uri(refs_main)}})
            client.notify("textDocument/didClose", {"textDocument": {"uri": to_uri(refs_main)}})

            print("LSP parity E2E checks passed.")
        finally:
            client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
