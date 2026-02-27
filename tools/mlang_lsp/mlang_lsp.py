#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from mlang_frontend.diagnostics import analyze_text
from mlang_frontend.semantic import (
    completion_symbols,
    definition_at,
    hover_symbol,
    references_at,
)
from mlang_frontend.source_map import line_char_to_offset, offset_to_line_char
from mlang_frontend.symbols import build_symbols
from mlang_frontend.workspace import WorkspaceIndex

REQUEST_CANCELLED = -32800


@dataclass
class Document:
    uri: str
    text: str
    version: int
    language_id: str


class JsonRpcServer:
    def __init__(self) -> None:
        self._documents: dict[str, Document] = {}
        self._workspace = WorkspaceIndex()
        self._running = True
        self._shutdown_requested = False
        self._cancelled_request_ids: set[Any] = set()

    def _read_message(self) -> dict[str, Any] | None:
        content_length = None
        while True:
            line = sys.stdin.buffer.readline()
            if not line:
                return None
            if line in (b"\r\n", b"\n"):
                break
            header = line.decode("ascii", errors="replace").strip()
            if header.lower().startswith("content-length:"):
                content_length = int(header.split(":", 1)[1].strip())
        if content_length is None:
            return None
        body = sys.stdin.buffer.read(content_length)
        if not body:
            return None
        return json.loads(body.decode("utf-8"))

    def _write(self, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload, separators=(",", ":"), ensure_ascii=False).encode(
            "utf-8"
        )
        header = f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii")
        sys.stdout.buffer.write(header)
        sys.stdout.buffer.write(raw)
        sys.stdout.buffer.flush()

    def _respond(self, req_id: Any, result: Any) -> None:
        self._write({"jsonrpc": "2.0", "id": req_id, "result": result})

    def _error(self, req_id: Any, code: int, message: str) -> None:
        self._write(
            {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {"code": code, "message": message},
            }
        )

    def _notify(self, method: str, params: dict[str, Any]) -> None:
        self._write({"jsonrpc": "2.0", "method": method, "params": params})

    def _publish_diagnostics(self, uri: str) -> None:
        doc = self._documents.get(uri)
        if doc is None:
            self._notify(
                "textDocument/publishDiagnostics", {"uri": uri, "diagnostics": []}
            )
            return

        diagnostics = [d.to_lsp(doc.text) for d in analyze_text(doc.text)]
        self._notify(
            "textDocument/publishDiagnostics",
            {"uri": uri, "diagnostics": diagnostics, "version": doc.version},
        )

    @staticmethod
    def _get_line(text: str, line: int) -> str:
        if line < 0:
            return ""
        lines = text.splitlines()
        if line >= len(lines):
            return ""
        return lines[line]

    @staticmethod
    def _word_at(text: str, offset: int) -> str:
        if not text:
            return ""
        offset = max(0, min(offset, len(text)))
        start = offset
        end = offset
        while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
            start -= 1
        while end < len(text) and (text[end].isalnum() or text[end] == "_"):
            end += 1
        return text[start:end]

    @staticmethod
    def _word_range_at(text: str, offset: int) -> tuple[int, int]:
        if not text:
            return offset, offset
        offset = max(0, min(offset, len(text)))
        start = offset
        end = offset
        while start > 0 and (text[start - 1].isalnum() or text[start - 1] == "_"):
            start -= 1
        while end < len(text) and (text[end].isalnum() or text[end] == "_"):
            end += 1
        return start, end

    @staticmethod
    def _keyword_items() -> list[dict[str, Any]]:
        keywords = [
            "fn",
            "let",
            "mut",
            "return",
            "if",
            "else",
            "while",
            "for",
            "true",
            "false",
        ]
        return [{"label": k, "kind": 14, "detail": "keyword"} for k in keywords]

    @staticmethod
    def _symbol_kind_to_lsp(sym_kind: str) -> int:
        if sym_kind == "function":
            return 12
        if sym_kind == "local_variable":
            return 13
        return 13

    @staticmethod
    def _symbol_detail(sym_kind: str, container: str) -> str:
        if sym_kind == "function":
            return "function"
        if sym_kind == "local_variable":
            return f"local variable ({container})" if container else "local variable"
        return "variable"

    @staticmethod
    def _location_from_offsets(uri: str, text: str, start: int, end: int) -> dict[str, Any]:
        s_line, s_char = offset_to_line_char(text, start)
        e_line, e_char = offset_to_line_char(text, end)
        return {
            "uri": uri,
            "range": {
                "start": {"line": s_line, "character": s_char},
                "end": {"line": e_line, "character": e_char},
            },
        }

    def _is_cancelled(self, req_id: Any) -> bool:
        return req_id in self._cancelled_request_ids

    def _consume_cancelled(self, req_id: Any) -> bool:
        if req_id in self._cancelled_request_ids:
            self._cancelled_request_ids.remove(req_id)
            return True
        return False

    def _progress_begin(self, token: Any, title: str, message: str) -> None:
        if token is None:
            return
        self._notify(
            "$/progress",
            {
                "token": token,
                "value": {
                    "kind": "begin",
                    "title": title,
                    "message": message,
                    "percentage": 0,
                },
            },
        )

    def _progress_report(self, token: Any, message: str, pct: int) -> None:
        if token is None:
            return
        self._notify(
            "$/progress",
            {
                "token": token,
                "value": {"kind": "report", "message": message, "percentage": pct},
            },
        )

    def _progress_end(self, token: Any, message: str) -> None:
        if token is None:
            return
        self._notify(
            "$/progress",
            {
                "token": token,
                "value": {"kind": "end", "message": message},
            },
        )

    def _handle_initialize(self, req_id: Any) -> None:
        self._respond(
            req_id,
            {
                "serverInfo": {"name": "mlang-lsp-scaffold", "version": "0.6.0"},
                "capabilities": {
                    "textDocumentSync": 2,
                    "hoverProvider": True,
                    "definitionProvider": True,
                    "referencesProvider": True,
                    "renameProvider": True,
                    "documentSymbolProvider": True,
                    "workspaceSymbolProvider": True,
                    "completionProvider": {
                        "resolveProvider": False,
                        "triggerCharacters": [".", ":"],
                    },
                    "diagnosticProvider": {
                        "interFileDependencies": False,
                        "workspaceDiagnostics": False,
                    },
                },
            },
        )

    def _handle_did_open(self, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        if not uri:
            return
        text = text_doc.get("text", "")
        version = int(text_doc.get("version", 0))
        self._documents[uri] = Document(
            uri=uri,
            text=text,
            version=version,
            language_id=text_doc.get("languageId", "mlang"),
        )
        self._workspace.open_document(uri, text, version)
        self._publish_diagnostics(uri)

    def _handle_did_change(self, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            return

        new_version = int(text_doc.get("version", doc.version))
        changes = params.get("contentChanges", [])
        new_text = self._workspace.apply_changes(
            uri, new_version, changes, line_char_to_offset
        )
        if new_text is None:
            return

        doc.text = new_text
        doc.version = new_version
        self._publish_diagnostics(uri)

    def _handle_hover(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        token = self._word_at(doc.text, offset)
        if token == "fn":
            value = "Define a function."
            self._respond(req_id, {"contents": {"kind": "markdown", "value": value}})
            return

        sym = hover_symbol(doc.text, token, offset) if token else None
        if sym is None and token:
            global_ref = self._workspace.find_global(token, prefer_uri=uri)
            if global_ref is not None:
                detail = self._symbol_detail(global_ref.symbol.kind, global_ref.symbol.container)
                value = f"{detail} `{global_ref.symbol.name}`"
                self._respond(req_id, {"contents": {"kind": "markdown", "value": value}})
                return

        if sym is None:
            if token:
                value = f"Symbol `{token}`."
                self._respond(req_id, {"contents": {"kind": "markdown", "value": value}})
            else:
                self._respond(req_id, None)
            return

        detail = self._symbol_detail(sym.kind, sym.container)
        value = f"{detail} `{sym.name}`"
        self._respond(req_id, {"contents": {"kind": "markdown", "value": value}})

    def _handle_completion(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"isIncomplete": False, "items": []})
            return

        pos = params.get("position", {})
        line = int(pos.get("line", 0))
        character = int(pos.get("character", 0))
        line_text = self._get_line(doc.text, line)
        prefix = line_text[: min(character, len(line_text))]
        m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", prefix)
        typed = m.group(1) if m else ""
        offset = line_char_to_offset(doc.text, line, character)

        items = self._keyword_items()
        for sym in completion_symbols(doc.text, offset):
            items.append(
                {
                    "label": sym.name,
                    "kind": self._symbol_kind_to_lsp(sym.kind),
                    "detail": self._symbol_detail(sym.kind, sym.container),
                }
            )

        if typed:
            items = [it for it in items if it["label"].startswith(typed)]
        self._respond(req_id, {"isIncomplete": False, "items": items})

    def _handle_definition(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._consume_cancelled(req_id):
            self._error(req_id, REQUEST_CANCELLED, "Request cancelled")
            return

        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        token = self._word_at(doc.text, offset)

        sym = definition_at(doc.text, offset)
        if sym is not None:
            loc = self._location_from_offsets(uri, doc.text, sym.start_offset, sym.end_offset)
            self._respond(req_id, loc)
            return

        g = self._workspace.find_global(token, prefer_uri=uri)
        if g is None:
            self._respond(req_id, None)
            return

        gdoc = self._documents.get(g.uri)
        if gdoc is None:
            self._respond(req_id, None)
            return
        self._respond(
            req_id,
            self._location_from_offsets(
                g.uri, gdoc.text, g.symbol.start_offset, g.symbol.end_offset
            ),
        )

    def _handle_references(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._consume_cancelled(req_id):
            self._error(req_id, REQUEST_CANCELLED, "Request cancelled")
            return

        token = params.get("workDoneToken")
        self._progress_begin(token, "Finding references", "Scanning workspace")

        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._progress_end(token, "No document")
            self._respond(req_id, [])
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        include_decl = params.get("context", {}).get("includeDeclaration", True)

        local_occ = references_at(doc.text, offset)
        if local_occ:
            if not include_decl:
                local_occ = [o for o in local_occ if not o.is_declaration]
            locs = [
                self._location_from_offsets(uri, doc.text, o.start_offset, o.end_offset)
                for o in local_occ
            ]
            self._progress_end(token, "Done")
            self._respond(req_id, locs)
            return

        word = self._word_at(doc.text, offset)
        g = self._workspace.find_global(word, prefer_uri=uri)
        if g is None:
            self._progress_end(token, "No symbol")
            self._respond(req_id, [])
            return

        def cancelled() -> bool:
            return self._is_cancelled(req_id)

        def on_progress(done: int, total: int) -> None:
            pct = 100 if total <= 0 else int((done * 100) / total)
            self._progress_report(token, f"Scanned {done}/{total} files", pct)

        occs = self._workspace.references_for_global(
            g.symbol.name,
            is_cancelled=cancelled,
            on_progress=on_progress,
        )
        if occs is None:
            self._consume_cancelled(req_id)
            self._progress_end(token, "Cancelled")
            self._error(req_id, REQUEST_CANCELLED, "Request cancelled")
            return

        if not include_decl:
            occs = [o for o in occs if not o.is_declaration]

        locs: list[dict[str, Any]] = []
        for o in occs:
            odoc = self._documents.get(o.uri)
            if odoc is None:
                continue
            locs.append(
                self._location_from_offsets(o.uri, odoc.text, o.start_offset, o.end_offset)
            )

        self._progress_end(token, "Done")
        self._respond(req_id, locs)

    def _handle_document_symbol(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, [])
            return

        symbols = build_symbols(doc.text)
        out: list[dict[str, Any]] = []
        for s in symbols:
            loc = self._location_from_offsets(uri, doc.text, s.start_offset, s.end_offset)
            out.append(
                {
                    "name": s.name,
                    "kind": self._symbol_kind_to_lsp(s.kind),
                    "location": loc,
                    "containerName": s.container,
                }
            )
        self._respond(req_id, out)

    def _handle_workspace_symbol(self, req_id: Any, params: dict[str, Any]) -> None:
        query = params.get("query", "")
        refs = self._workspace.search_globals(str(query), limit=200)
        out: list[dict[str, Any]] = []
        for ref in refs:
            doc = self._documents.get(ref.uri)
            if doc is None:
                continue
            out.append(
                {
                    "name": ref.symbol.name,
                    "kind": self._symbol_kind_to_lsp(ref.symbol.kind),
                    "location": self._location_from_offsets(
                        ref.uri,
                        doc.text,
                        ref.symbol.start_offset,
                        ref.symbol.end_offset,
                    ),
                    "containerName": ref.symbol.container,
                }
            )
        self._respond(req_id, out)

    def _handle_prepare_rename(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )
        token = self._word_at(doc.text, offset)
        if not token:
            self._respond(req_id, None)
            return

        local = definition_at(doc.text, offset)
        global_ref = self._workspace.find_global(token, prefer_uri=uri)
        if local is None and global_ref is None:
            self._respond(req_id, None)
            return

        start, end = self._word_range_at(doc.text, offset)
        rng = self._location_from_offsets(uri, doc.text, start, end)["range"]
        self._respond(req_id, {"range": rng, "placeholder": token})

    def _handle_rename(self, req_id: Any, params: dict[str, Any]) -> None:
        if self._consume_cancelled(req_id):
            self._error(req_id, REQUEST_CANCELLED, "Request cancelled")
            return

        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, None)
            return

        new_name = str(params.get("newName", "")).strip()
        if not new_name:
            self._respond(req_id, None)
            return

        pos = params.get("position", {})
        offset = line_char_to_offset(
            doc.text, int(pos.get("line", 0)), int(pos.get("character", 0))
        )

        local_occ = references_at(doc.text, offset)
        changes: dict[str, list[dict[str, Any]]] = {}
        if local_occ:
            edits: list[dict[str, Any]] = []
            for o in local_occ:
                rng = self._location_from_offsets(
                    uri, doc.text, o.start_offset, o.end_offset
                )["range"]
                edits.append({"range": rng, "newText": new_name})
            changes[uri] = edits
            self._respond(req_id, {"changes": changes})
            return

        token = self._word_at(doc.text, offset)
        global_ref = self._workspace.find_global(token, prefer_uri=uri)
        if global_ref is None:
            self._respond(req_id, None)
            return

        occs = self._workspace.references_for_global(global_ref.symbol.name)
        if occs is None:
            self._error(req_id, REQUEST_CANCELLED, "Request cancelled")
            return

        for o in occs:
            odoc = self._documents.get(o.uri)
            if odoc is None:
                continue
            rng = self._location_from_offsets(
                o.uri, odoc.text, o.start_offset, o.end_offset
            )["range"]
            changes.setdefault(o.uri, []).append({"range": rng, "newText": new_name})

        if not changes:
            self._respond(req_id, None)
            return
        self._respond(req_id, {"changes": changes})

    def _handle_diagnostic(self, req_id: Any, params: dict[str, Any]) -> None:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri", "")
        doc = self._documents.get(uri)
        if doc is None:
            self._respond(req_id, {"kind": "full", "items": []})
            return
        items = [d.to_lsp(doc.text) for d in analyze_text(doc.text)]
        self._respond(req_id, {"kind": "full", "items": items})

    def _dispatch_request(self, req_id: Any, method: str, params: dict[str, Any]) -> None:
        if method == "initialize":
            self._handle_initialize(req_id)
        elif method == "shutdown":
            self._shutdown_requested = True
            self._respond(req_id, None)
        elif method == "textDocument/hover":
            self._handle_hover(req_id, params)
        elif method == "textDocument/completion":
            self._handle_completion(req_id, params)
        elif method == "textDocument/definition":
            self._handle_definition(req_id, params)
        elif method == "textDocument/references":
            self._handle_references(req_id, params)
        elif method == "textDocument/prepareRename":
            self._handle_prepare_rename(req_id, params)
        elif method == "textDocument/rename":
            self._handle_rename(req_id, params)
        elif method == "textDocument/documentSymbol":
            self._handle_document_symbol(req_id, params)
        elif method == "workspace/symbol":
            self._handle_workspace_symbol(req_id, params)
        elif method == "textDocument/diagnostic":
            self._handle_diagnostic(req_id, params)
        else:
            self._error(req_id, -32601, f"Method not found: {method}")

    def _dispatch_notification(self, method: str, params: dict[str, Any]) -> None:
        if method == "exit":
            self._running = False
            return
        if method == "initialized":
            return
        if method == "$/cancelRequest":
            req_id = params.get("id")
            if req_id is not None:
                self._cancelled_request_ids.add(req_id)
            return
        if method == "textDocument/didOpen":
            self._handle_did_open(params)
            return
        if method == "textDocument/didChange":
            self._handle_did_change(params)
            return
        if method == "textDocument/didSave":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri", "")
            if uri:
                self._publish_diagnostics(uri)
            return
        if method == "textDocument/didClose":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri", "")
            if uri in self._documents:
                del self._documents[uri]
                self._workspace.remove_document(uri)
                self._publish_diagnostics(uri)
            return

    def run(self) -> int:
        while self._running:
            msg = self._read_message()
            if msg is None:
                break
            method = msg.get("method")
            if not method:
                continue
            params = msg.get("params", {})
            if "id" in msg:
                self._dispatch_request(msg["id"], method, params)
            else:
                self._dispatch_notification(method, params)
        return 0 if self._shutdown_requested else 1


def main() -> int:
    return JsonRpcServer().run()


if __name__ == "__main__":
    raise SystemExit(main())
