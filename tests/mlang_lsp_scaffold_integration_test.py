#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "tools" / "mlang_lsp" / "mlang_lsp.py"
REQUEST_CANCELLED = -32800


class LspHarness:
    def __init__(self) -> None:
        cmd = self._resolve_server_cmd()
        if not cmd:
            raise unittest.SkipTest(
                "mlang LSP scaffold server not found at tools/mlang_lsp/mlang_lsp.py"
            )
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=False,
        )
        self._next_id = 1

    @staticmethod
    def _resolve_server_cmd() -> List[str]:
        env_cmd = os.environ.get("MLANG_LSP_SCAFFOLD_SERVER", "").strip()
        if env_cmd:
            parts = env_cmd.split()
            if parts:
                return parts

        if SERVER.exists():
            return [sys.executable, str(SERVER)]

        return []

    def close(self) -> None:
        if self.proc.poll() is None:
            try:
                self.request("shutdown", {})
            except Exception:
                pass
            self.notify("exit", {})
            self.proc.wait(timeout=2)
        if self.proc.stdin:
            self.proc.stdin.close()
        if self.proc.stdout:
            self.proc.stdout.close()
        if self.proc.stderr:
            self.proc.stderr.close()

    def _write(self, msg: Dict[str, Any]) -> None:
        assert self.proc.stdin is not None
        payload = json.dumps(msg, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
        header = f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii")
        self.proc.stdin.write(header)
        self.proc.stdin.write(payload)
        self.proc.stdin.flush()

    def _read(self) -> Dict[str, Any]:
        assert self.proc.stdout is not None
        content_length: Optional[int] = None
        while True:
            line = self.proc.stdout.readline()
            if not line:
                details = "LSP server closed stdout unexpectedly"
                if self.proc.stderr is not None:
                    err = self.proc.stderr.read().decode("utf-8", errors="replace").strip()
                    if err:
                        details += f" | stderr: {err}"
                raise RuntimeError(details)
            if line in (b"\r\n", b"\n"):
                break
            head = line.decode("ascii").strip()
            if head.lower().startswith("content-length:"):
                content_length = int(head.split(":", 1)[1].strip())
        if content_length is None:
            raise RuntimeError("Missing Content-Length")
        body = self.proc.stdout.read(content_length)
        if not body:
            raise RuntimeError("Missing payload body")
        return json.loads(body.decode("utf-8"))

    def request(
        self,
        method: str,
        params: Dict[str, Any],
    ) -> Dict[str, Any]:
        result, error, _notes = self.request_with_meta(method, params)
        if error is not None:
            raise RuntimeError(f"LSP error for {method}: {error}")
        return result

    def request_with_meta(
        self,
        method: str,
        params: Dict[str, Any],
        *,
        req_id: Optional[int] = None,
        collect_methods: Optional[Set[str]] = None,
    ) -> Tuple[Dict[str, Any], Optional[Dict[str, Any]], List[Dict[str, Any]]]:
        if req_id is None:
            req_id = self._next_id
            self._next_id += 1
        self._write({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})

        notifications: List[Dict[str, Any]] = []
        while True:
            msg = self._read()
            if msg.get("id") == req_id:
                return msg.get("result"), msg.get("error"), notifications
            if collect_methods and msg.get("method") in collect_methods:
                notifications.append(msg)

    def notify(self, method: str, params: Dict[str, Any]) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def read_until_notification(self, method: str, max_reads: int = 20) -> Dict[str, Any]:
        for _ in range(max_reads):
            msg = self._read()
            if msg.get("method") == method:
                return msg.get("params", {})
        raise RuntimeError(f"Did not receive notification: {method}")


class MlangLspScaffoldIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.h = LspHarness()

    def tearDown(self) -> None:
        self.h.close()

    def test_initialize_hover_completion_and_diagnostics(self) -> None:
        init = self.h.request(
            "initialize",
            {
                "processId": None,
                "rootUri": None,
                "capabilities": {},
                "clientInfo": {"name": "mlang-lsp-test", "version": "1"},
            },
        )
        caps = init.get("capabilities", {})
        self.assertTrue(caps.get("hoverProvider"))
        self.assertTrue(caps.get("definitionProvider"))
        self.assertTrue(caps.get("referencesProvider"))
        self.assertTrue(caps.get("renameProvider"))
        self.assertIn("signatureHelpProvider", caps)
        self.assertIn("semanticTokensProvider", caps)
        self.assertIn("completionProvider", caps)

        self.h.notify("initialized", {})

        uri = "file:///tmp/test.mlang"
        source = "fn main() {\n  let value = 1\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        open_diag = self.h.read_until_notification("textDocument/publishDiagnostics")
        self.assertEqual(open_diag.get("uri"), uri)
        self.assertEqual(open_diag.get("diagnostics"), [])

        hover = self.h.request(
            "textDocument/hover",
            {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 1}},
        )
        self.assertIn("Define a function", hover["contents"]["value"])

        keyword_completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 4}},
        )
        labels = {item.get("label") for item in keyword_completion.get("items", [])}
        self.assertIn("let", labels)

        symbol_completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 10}},
        )
        labels = {item.get("label") for item in symbol_completion.get("items", [])}
        self.assertIn("value", labels)

        bad_uri = "file:///tmp/bad.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": bad_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  let s = \"oops\n}\n",
                }
            },
        )
        bad_diag = self.h.read_until_notification("textDocument/publishDiagnostics")
        self.assertEqual(bad_diag.get("uri"), bad_uri)
        self.assertGreater(len(bad_diag.get("diagnostics", [])), 0)

    def test_incremental_did_change_range_patching(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/inc.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  let valeu = 1\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        self.h.notify(
            "textDocument/didChange",
            {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [
                    {
                        "range": {
                            "start": {"line": 1, "character": 6},
                            "end": {"line": 1, "character": 11},
                        },
                        "text": "value",
                    }
                ],
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 11}},
        )
        labels = {item.get("label") for item in completion.get("items", [])}
        self.assertIn("value", labels)
        self.assertNotIn("valeu", labels)

    def test_scope_aware_completion_and_hover(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/scope.mlang"
        source = (
            "let top = 1\n"
            "fn a() {\n"
            "  let only_a = 1\n"
            "  only_a\n"
            "}\n"
            "fn b() {\n"
            "  let only_b = 2\n"
            "}\n"
        )
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        completion = self.h.request(
            "textDocument/completion",
            {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 8}},
        )
        labels = {item.get("label") for item in completion.get("items", [])}
        self.assertIn("only_a", labels)
        self.assertNotIn("only_b", labels)

        hover = self.h.request(
            "textDocument/hover",
            {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 4}},
        )
        self.assertIn("local variable", hover["contents"]["value"])

    def test_definition_and_references(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        uri = "file:///tmp/refs.mlang"
        source = "fn main() {\n  let x = 1\n  x\n  x\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": source}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        definition = self.h.request(
            "textDocument/definition",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 2}},
        )
        self.assertEqual(definition["uri"], uri)
        self.assertEqual(definition["range"]["start"]["line"], 1)
        self.assertEqual(definition["range"]["start"]["character"], 6)

        references = self.h.request(
            "textDocument/references",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 2},
                "context": {"includeDeclaration": True},
            },
        )
        starts = sorted((r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in references)
        self.assertEqual(starts, [(1, 6), (2, 2), (3, 2)])

        references_no_decl = self.h.request(
            "textDocument/references",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 2},
                "context": {"includeDeclaration": False},
            },
        )
        starts_no_decl = sorted((r["range"]["start"]["line"], r["range"]["start"]["character"]) for r in references_no_decl)
        self.assertEqual(starts_no_decl, [(2, 2), (3, 2)])

    def test_cross_file_definition_and_references(self) -> None:
        self.h.request(
            "initialize",
            {"processId": None, "rootUri": None, "capabilities": {}},
        )
        self.h.notify("initialized", {})

        defs_uri = "file:///tmp/defs.mlang"
        use_uri = "file:///tmp/use.mlang"

        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": defs_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn helper() {}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": use_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  helper()\n  helper()\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        definition = self.h.request(
            "textDocument/definition",
            {
                "textDocument": {"uri": use_uri},
                "position": {"line": 1, "character": 3},
            },
        )
        self.assertEqual(definition["uri"], defs_uri)
        self.assertEqual(definition["range"]["start"]["line"], 0)

        refs = self.h.request(
            "textDocument/references",
            {
                "textDocument": {"uri": use_uri},
                "position": {"line": 1, "character": 3},
                "context": {"includeDeclaration": True},
            },
        )
        by_uri = sorted((r["uri"], r["range"]["start"]["line"]) for r in refs)
        self.assertEqual(by_uri, [(defs_uri, 0), (use_uri, 1), (use_uri, 2)])

    def test_references_progress_notifications(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        defs_uri = "file:///tmp/prog_defs.mlang"
        use_uri = "file:///tmp/prog_use.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": defs_uri, "languageId": "mlang", "version": 1, "text": "fn helper() {}\n"}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")
        self.h.notify(
            "textDocument/didOpen",
            {"textDocument": {"uri": use_uri, "languageId": "mlang", "version": 1, "text": "fn main() { helper() }\n"}},
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        result, error, notes = self.h.request_with_meta(
            "textDocument/references",
            {
                "textDocument": {"uri": use_uri},
                "position": {"line": 0, "character": 14},
                "context": {"includeDeclaration": True},
                "workDoneToken": "tok-progress",
            },
            collect_methods={"$/progress"},
        )
        self.assertIsNone(error)
        self.assertTrue(result)
        kinds = [n.get("params", {}).get("value", {}).get("kind") for n in notes]
        self.assertIn("begin", kinds)
        self.assertIn("end", kinds)

    def test_request_cancellation(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        uri = "file:///tmp/cancel.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  let x = 1\n  x\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        rid = 9001
        self.h.notify("$/cancelRequest", {"id": rid})
        _res, err, _notes = self.h.request_with_meta(
            "textDocument/references",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 2},
                "context": {"includeDeclaration": True},
            },
            req_id=rid,
        )
        self.assertIsNotNone(err)
        assert err is not None
        self.assertEqual(err.get("code"), REQUEST_CANCELLED)

    def test_document_symbol_and_workspace_symbol(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        defs_uri = "file:///tmp/symbols_defs.mlang"
        use_uri = "file:///tmp/symbols_use.mlang"

        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": defs_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "let global_value = 1\nfn helper() {}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": use_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  let local_value = helper()\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        doc_symbols = self.h.request(
            "textDocument/documentSymbol",
            {"textDocument": {"uri": use_uri}},
        )
        names = {item.get("name") for item in doc_symbols}
        self.assertIn("main", names)
        self.assertIn("local_value", names)

        ws_symbols = self.h.request("workspace/symbol", {"query": "help"})
        ws_names = [item.get("name") for item in ws_symbols]
        self.assertIn("helper", ws_names)

    def test_prepare_rename_and_rename_local(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        uri = "file:///tmp/rename_local.mlang"
        source = "fn main() {\n  let old_name = 1\n  old_name\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": source,
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        prep = self.h.request(
            "textDocument/prepareRename",
            {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 5}},
        )
        self.assertEqual(prep["placeholder"], "old_name")

        edit = self.h.request(
            "textDocument/rename",
            {
                "textDocument": {"uri": uri},
                "position": {"line": 2, "character": 5},
                "newName": "new_name",
            },
        )
        self.assertIn("changes", edit)
        self.assertIn(uri, edit["changes"])
        edits = edit["changes"][uri]
        starts = sorted((e["range"]["start"]["line"], e["range"]["start"]["character"]) for e in edits)
        self.assertEqual(starts, [(1, 6), (2, 2)])
        self.assertTrue(all(e["newText"] == "new_name" for e in edits))

    def test_rename_global_cross_file(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        defs_uri = "file:///tmp/rename_defs.mlang"
        use_uri = "file:///tmp/rename_use.mlang"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": defs_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn helper() {}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": use_uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": "fn main() {\n  helper()\n}\n",
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        edit = self.h.request(
            "textDocument/rename",
            {
                "textDocument": {"uri": use_uri},
                "position": {"line": 1, "character": 3},
                "newName": "helper2",
            },
        )
        self.assertIn("changes", edit)
        self.assertIn(defs_uri, edit["changes"])
        self.assertIn(use_uri, edit["changes"])
        self.assertEqual(edit["changes"][defs_uri][0]["newText"], "helper2")
        self.assertEqual(edit["changes"][use_uri][0]["newText"], "helper2")

    def test_signature_help(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        uri = "file:///tmp/signature_help.mlang"
        source = "fn main() {\n  helper(a, b)\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": source,
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        sig = self.h.request(
            "textDocument/signatureHelp",
            {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 12}},
        )
        self.assertTrue(sig.get("signatures"))
        self.assertEqual(sig.get("activeSignature"), 0)
        self.assertEqual(sig.get("activeParameter"), 1)
        self.assertIn("helper(", sig["signatures"][0]["label"])

    def test_semantic_tokens_full(self) -> None:
        self.h.request("initialize", {"processId": None, "rootUri": None, "capabilities": {}})
        self.h.notify("initialized", {})

        uri = "file:///tmp/sem_tokens.mlang"
        source = "fn main() {\n  let value = 1\n  return value\n}\n"
        self.h.notify(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "mlang",
                    "version": 1,
                    "text": source,
                }
            },
        )
        self.h.read_until_notification("textDocument/publishDiagnostics")

        tokens = self.h.request(
            "textDocument/semanticTokens/full",
            {"textDocument": {"uri": uri}},
        )
        data = tokens.get("data", [])
        self.assertIsInstance(data, list)
        self.assertGreater(len(data), 0)
        self.assertEqual(len(data) % 5, 0)



if __name__ == "__main__":
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    unittest.main(verbosity=2)
