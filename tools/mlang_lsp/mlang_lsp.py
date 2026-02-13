#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple


@dataclass
class Symbol:
    name: str
    kind: int
    uri: str
    line: int
    character: int


@dataclass
class FieldInfo:
    symbol: Symbol
    type_name: str


@dataclass
class StructInfo:
    name: str
    start_line: int
    end_line: int
    body_depth: int
    base_name: Optional[str] = None
    fields: Dict[str, FieldInfo] = field(default_factory=dict)


@dataclass
class FunctionInfo:
    name: str
    start_line: int
    end_line: int
    body_depth: int
    var_types: Dict[str, str] = field(default_factory=dict)


LSP_SYMBOL_KIND_MODULE = 2
LSP_SYMBOL_KIND_FIELD = 8
LSP_SYMBOL_KIND_FUNCTION = 12
LSP_SYMBOL_KIND_STRUCT = 23


WORD_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
MLANG_SOURCE_EXTS = (".mla", ".mlastub")

MLANG_FORMAT_DIR = os.path.join(os.path.dirname(__file__), "..", "mlang_format")
if MLANG_FORMAT_DIR not in sys.path:
    sys.path.insert(0, MLANG_FORMAT_DIR)
try:
    import mlang_format
except ImportError:
    mlang_format = None


class MlangLspServer:
    def __init__(self) -> None:
        self.root_path: Optional[str] = None
        self.documents: Dict[str, str] = {}
        self.symbols_by_name: Dict[str, List[Symbol]] = {}
        self.symbols_by_uri: Dict[str, List[Symbol]] = {}
        self.structs_by_uri: Dict[str, List[StructInfo]] = {}
        self.structs_by_name: Dict[str, List[StructInfo]] = {}
        self.functions_by_uri: Dict[str, List[FunctionInfo]] = {}
        self.function_returns_by_name: Dict[str, List[Tuple[str, str]]] = {}
        self.file_by_stem: Dict[str, str] = {}

    def run(self) -> None:
        while True:
            payload = self._read_message()
            if payload is None:
                break
            try:
                msg = json.loads(payload)
            except json.JSONDecodeError:
                continue

            if "method" in msg:
                method = msg.get("method")
                if "id" in msg:
                    result = self._handle_request(method, msg.get("params", {}))
                    self._send_response(msg["id"], result)
                else:
                    self._handle_notification(method, msg.get("params", {}))

    def _handle_request(self, method: str, params: dict) -> object:
        if method == "initialize":
            self._handle_initialize(params)
            return {
                "capabilities": {
                    "textDocumentSync": {"openClose": True, "change": 1},
                    "definitionProvider": True,
                    "referencesProvider": True,
                    "documentSymbolProvider": True,
                    "workspaceSymbolProvider": True,
                    "documentFormattingProvider": True,
                    "semanticTokensProvider": {
                        "legend": {
                            "tokenTypes": ["keyword"],
                            "tokenModifiers": [],
                        },
                        "full": True,
                    },
                }
            }
        if method == "shutdown":
            return None
        if method == "textDocument/definition":
            return self._handle_definition(params)
        if method == "textDocument/references":
            return self._handle_references(params)
        if method == "textDocument/documentSymbol":
            return self._handle_document_symbols(params)
        if method == "workspace/symbol":
            return self._handle_workspace_symbols(params)
        if method == "textDocument/formatting":
            return self._handle_formatting(params)
        if method == "textDocument/semanticTokens/full":
            return self._handle_semantic_tokens(params)
        return None

    def _handle_notification(self, method: str, params: dict) -> None:
        if method == "exit":
            raise SystemExit(0)
        if method == "initialized":
            return
        if method == "textDocument/didOpen":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            text = text_doc.get("text", "")
            if uri:
                self.documents[uri] = text
                self._index_document(uri, text)
            return
        if method == "textDocument/didChange":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            changes = params.get("contentChanges", [])
            if uri and changes:
                text = changes[-1].get("text", "")
                self.documents[uri] = text
                self._index_document(uri, text)
            return
        if method == "textDocument/didSave":
            text_doc = params.get("textDocument", {})
            uri = text_doc.get("uri")
            if uri:
                text = self._read_uri_text(uri)
                if text is not None:
                    self.documents[uri] = text
                    self._index_document(uri, text)
            return

    def _handle_initialize(self, params: dict) -> None:
        root_uri = params.get("rootUri")
        root_path = params.get("rootPath")
        if root_uri:
            self.root_path = self._uri_to_path(root_uri)
        elif root_path:
            self.root_path = root_path
        else:
            self.root_path = os.getcwd()
        self._scan_workspace()

    def _handle_definition(self, params: dict) -> Optional[object]:
        uri, line, character = self._extract_position(params)
        if not uri:
            return None
        text = self.documents.get(uri)
        if text is None:
            text = self._read_uri_text(uri)
        if text is None:
            return None

        word, start_idx = self._word_at_position_with_index(text, line, character)
        attr = self._attribute_at_position(text, line, character)
        if attr and (not word or word in {"test", "derive"}):
            loc = self._runtime_attribute_location(attr)
            if loc:
                return [loc]
        if not word:
            return None

        chain = self._extract_access_chain(text, line, start_idx, word)
        if chain:
            resolved = self._resolve_chain_definition(uri, line, chain)
            if resolved:
                return [self._symbol_to_location(resolved)]

        locations = []
        for sym in self.symbols_by_name.get(word, []):
            locations.append(self._symbol_to_location(sym))

        if not locations:
            stem_path = self.file_by_stem.get(word)
            if stem_path:
                locations.append(self._path_to_location(stem_path))

        if not locations:
            return None
        return locations

    def _attribute_at_position(
        self, text: str, line: int, character_utf16: int
    ) -> Optional[str]:
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return None
        line_text = lines[line]
        idx = self._utf16_to_index(line_text, character_utf16)
        if idx < 0 or idx > len(line_text):
            return None
        for attr in ("#[test]", "#[derive(Debug)]"):
            start = 0
            while True:
                pos = line_text.find(attr, start)
                if pos == -1:
                    break
                end = pos + len(attr)
                if pos <= idx <= end:
                    return attr
                start = end
        return None

    def _runtime_attribute_location(self, attr: str) -> Optional[object]:
        if not self.root_path:
            return None
        docs_path = os.path.join(self.root_path, "docs", "language_attributes.mlastub")
        if not os.path.isfile(docs_path):
            docs_path = os.path.join(self.root_path, "docs", "runtime_builtins.mlastub")
        if not os.path.isfile(docs_path):
            return None
        try:
            with open(docs_path, "r", encoding="utf-8") as handle:
                for line_no, line in enumerate(handle):
                    col = line.find(attr)
                    if col != -1:
                        return {
                            "uri": self._path_to_uri(docs_path),
                            "range": {
                                "start": {"line": line_no, "character": col},
                                "end": {"line": line_no, "character": col + len(attr)},
                            },
                        }
        except OSError:
            return None
        return None

    def _handle_references(self, params: dict) -> List[object]:
        uri, line, character = self._extract_position(params)
        if not uri:
            return []
        text = self.documents.get(uri)
        if text is None:
            text = self._read_uri_text(uri)
        if text is None:
            return []

        word, _ = self._word_at_position_with_index(text, line, character)
        if not word:
            return []

        include_decl = params.get("context", {}).get("includeDeclaration", True)
        locations: List[object] = []
        for doc_uri, doc_text in self._all_documents_with_disk():
            locations.extend(self._find_word_locations(doc_uri, doc_text, word))

        if not include_decl:
            locations = [loc for loc in locations if not self._is_definition(loc)]
        return locations

    def _handle_document_symbols(self, params: dict) -> List[object]:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri")
        if not uri:
            return []
        symbols = self.symbols_by_uri.get(uri, [])
        return [self._symbol_to_document_symbol(sym) for sym in symbols]

    def _handle_workspace_symbols(self, params: dict) -> List[object]:
        query = params.get("query", "")
        out = []
        for name, syms in self.symbols_by_name.items():
            if query and query not in name:
                continue
            for sym in syms:
                out.append(self._symbol_to_workspace_symbol(sym))
        return out

    def _handle_formatting(self, params: dict) -> List[object]:
        if mlang_format is None:
            return []
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri")
        if not uri:
            return []
        text = self.documents.get(uri)
        if text is None:
            text = self._read_uri_text(uri)
        if text is None:
            return []
        path = self._uri_to_path(uri)
        config_path = mlang_format.find_config(
            path or (self.root_path or os.getcwd()),
            root_path=self.root_path,
        )
        config = mlang_format.load_config(config_path)
        formatted = mlang_format.format_text(text, config)
        if formatted == text:
            return []
        return [
            {
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": self._end_position(text),
                },
                "newText": formatted,
            }
        ]

    def _handle_semantic_tokens(self, params: dict) -> dict:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri")
        if not uri:
            return {"data": []}
        text = self.documents.get(uri)
        if text is None:
            text = self._read_uri_text(uri)
        if text is None:
            return {"data": []}

        tokens: List[Tuple[int, int, int, int, int]] = []
        for line_no, line in enumerate(text.splitlines()):
            scan_limit = len(line)
            comment_pos = line.find("//")
            if comment_pos != -1:
                scan_limit = comment_pos
            attr_specs = (
                ("#[derive(Debug)]", 2, 6),  # derive
                ("#[test]", 2, 4),          # test
            )
            for attr, word_offset, word_len in attr_specs:
                start = 0
                while True:
                    idx = line.find(attr, start)
                    if idx == -1:
                        break
                    end = idx + len(attr)
                    if end <= scan_limit:
                        tokens.append((line_no, idx + word_offset, word_len, 0, 0))
                    start = end

        tokens.sort(key=lambda t: (t[0], t[1]))
        data: List[int] = []
        prev_line = 0
        prev_start = 0
        first = True
        for line_no, start, length, token_type, mods in tokens:
            delta_line = line_no if first else line_no - prev_line
            delta_start = start if first or delta_line != 0 else start - prev_start
            data.extend([delta_line, delta_start, length, token_type, mods])
            prev_line = line_no
            prev_start = start
            first = False
        return {"data": data}

    @staticmethod
    def _end_position(text: str) -> Dict[str, int]:
        lines = text.split("\n")
        return {"line": len(lines) - 1, "character": len(lines[-1])}

    def _scan_workspace(self) -> None:
        if not self.root_path:
            return
        root = self.root_path
        self.symbols_by_name.clear()
        self.symbols_by_uri.clear()
        self.structs_by_uri.clear()
        self.structs_by_name.clear()
        self.functions_by_uri.clear()
        self.function_returns_by_name.clear()
        self.file_by_stem.clear()

        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [
                d for d in dirnames if d not in {".git", "build", "out", "dist"}
            ]
            for fname in filenames:
                if not fname.endswith(MLANG_SOURCE_EXTS):
                    continue
                path = os.path.join(dirpath, fname)
                uri = self._path_to_uri(path)
                if fname.endswith(".mla"):
                    self.file_by_stem[os.path.splitext(fname)[0]] = path
                text = self._read_path_text(path)
                if text is None:
                    continue
                self._index_document(uri, text)

    def _index_document(self, uri: str, text: str) -> None:
        symbols, structs, functions, fn_returns = self._extract_symbols(text, uri)
        self.symbols_by_uri[uri] = symbols
        self.structs_by_uri[uri] = structs
        self.functions_by_uri[uri] = functions

        # Rebuild symbol name index for this uri
        for name in list(self.symbols_by_name.keys()):
            self.symbols_by_name[name] = [
                s for s in self.symbols_by_name[name] if s.uri != uri
            ]
            if not self.symbols_by_name[name]:
                del self.symbols_by_name[name]

        for sym in symbols:
            self.symbols_by_name.setdefault(sym.name, []).append(sym)

        # Rebuild struct name index for this uri
        for name in list(self.structs_by_name.keys()):
            self.structs_by_name[name] = [
                s for s in self.structs_by_name[name] if s.name != name or s not in structs
            ]
            if not self.structs_by_name[name]:
                del self.structs_by_name[name]

        for struct in structs:
            self.structs_by_name.setdefault(struct.name, []).append(struct)

        # Rebuild function return types for this uri
        for name in list(self.function_returns_by_name.keys()):
            self.function_returns_by_name[name] = [
                (u, t) for (u, t) in self.function_returns_by_name[name] if u != uri
            ]
            if not self.function_returns_by_name[name]:
                del self.function_returns_by_name[name]

        for name, ret in fn_returns:
            self.function_returns_by_name.setdefault(name, []).append((uri, ret))

    def _extract_symbols(
        self, text: str, uri: str
    ) -> Tuple[List[Symbol], List[StructInfo], List[FunctionInfo], List[Tuple[str, str]]]:
        cleaned = self._strip_comments_and_strings(text)
        symbols: List[Symbol] = []

        structs, field_symbols = self._extract_structs_and_fields(cleaned, uri)
        symbols.extend(field_symbols)

        functions, fn_returns = self._extract_functions_and_types(cleaned, uri, structs)

        for line_no, line in enumerate(cleaned.splitlines()):
            symbols.extend(self._extract_line_symbols(line, uri, line_no))
        return symbols, structs, functions, fn_returns

    def _extract_structs_and_fields(
        self, cleaned: str, uri: str
    ) -> Tuple[List[StructInfo], List[Symbol]]:
        structs: List[StructInfo] = []
        field_symbols: List[Symbol] = []
        struct_stack: List[StructInfo] = []
        pending_struct: Optional[Tuple[str, int, Optional[str]]] = None
        depth = 0

        lines = cleaned.splitlines()
        for line_no, line in enumerate(lines):
            line_depth = depth

            if pending_struct is None:
                m = re.search(
                    r"\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*:\s*([A-Za-z_][A-Za-z0-9_]*))?",
                    line,
                )
                if m:
                    pending_struct = (m.group(1), line_no, m.group(2))

            if struct_stack and line_depth == struct_stack[-1].body_depth:
                m = re.match(r"\s*var\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^;=]+)", line)
                if m:
                    field_name = m.group(1)
                    type_raw = m.group(2)
                    type_name = self._normalize_type(type_raw)
                    col = line.find(field_name)
                    sym = Symbol(
                        name=field_name,
                        kind=LSP_SYMBOL_KIND_FIELD,
                        uri=uri,
                        line=line_no,
                        character=col,
                    )
                    field_symbols.append(sym)
                    struct_stack[-1].fields[field_name] = FieldInfo(
                        symbol=sym, type_name=type_name
                    )

            for ch in line:
                if ch == '{':
                    depth += 1
                    if pending_struct is not None:
                        name, start_line, base_name = pending_struct
                        struct_stack.append(
                            StructInfo(
                                name=name,
                                start_line=start_line,
                                end_line=-1,
                                body_depth=depth,
                                base_name=base_name,
                            )
                        )
                        pending_struct = None
                elif ch == '}':
                    if struct_stack and struct_stack[-1].body_depth == depth:
                        struct = struct_stack.pop()
                        struct.end_line = line_no
                        structs.append(struct)
                    if depth > 0:
                        depth -= 1

        return structs, field_symbols

    def _extract_functions_and_types(
        self, cleaned: str, uri: str, structs: List[StructInfo]
    ) -> Tuple[List[FunctionInfo], List[Tuple[str, str]]]:
        functions: List[FunctionInfo] = []
        fn_returns: List[Tuple[str, str]] = []
        pending_fn: Optional[Tuple[str, int, str, str]] = None
        func_stack: List[FunctionInfo] = []
        depth = 0

        lines = cleaned.splitlines()
        for line_no, line in enumerate(lines):
            if pending_fn is None:
                m = re.search(
                    r"\b(?:(extern)\s+)?(?:pub\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*->\s*([^\s{;]+)",
                    line,
                )
                if m:
                    is_extern = m.group(1) is not None
                    name = m.group(2)
                    params = m.group(3)
                    ret_raw = m.group(4)
                    ret_type = self._normalize_type(ret_raw)
                    fn_returns.append((name, ret_type))
                    if not is_extern:
                        pending_fn = (name, line_no, params, ret_type)

            for ch in line:
                if ch == '{':
                    depth += 1
                    if pending_fn is not None:
                        name, start_line, params, _ret = pending_fn
                        fn = FunctionInfo(
                            name=name,
                            start_line=start_line,
                            end_line=-1,
                            body_depth=depth,
                        )
                        fn.var_types.update(self._parse_params(params))
                        enclosing = self._find_enclosing_struct_from_list(structs, line_no)
                        if enclosing:
                            fn.var_types.setdefault("self", enclosing.name)
                        func_stack.append(fn)
                        pending_fn = None
                elif ch == '}':
                    if func_stack and func_stack[-1].body_depth == depth:
                        fn = func_stack.pop()
                        fn.end_line = line_no
                        functions.append(fn)
                    if depth > 0:
                        depth -= 1

            if func_stack:
                self._parse_vars_and_inference(func_stack[-1], line, uri)

        return functions, fn_returns

    def _parse_vars_and_inference(self, fn: FunctionInfo, line: str, uri: str) -> None:
        m = re.search(r"\b(let|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^;=]+)", line)
        if m:
            name = m.group(2)
            type_name = self._normalize_type(m.group(3))
            if type_name:
                fn.var_types[name] = type_name

        m = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\{", line)
        if m:
            name = m.group(1)
            type_name = self._normalize_type(m.group(2))
            if type_name:
                fn.var_types.setdefault(name, type_name)

        m = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(", line)
        if m:
            name = m.group(1)
            fn_name = m.group(2)
            ret_type = self._find_function_return_type(fn_name, uri)
            if ret_type:
                fn.var_types.setdefault(name, ret_type)

    def _extract_line_symbols(self, line: str, uri: str, line_no: int) -> List[Symbol]:
        symbols: List[Symbol] = []
        patterns = [
            (r"^\s*pub\s+struct\s+([A-Za-z_][A-Za-z0-9_]*)", LSP_SYMBOL_KIND_STRUCT),
            (r"^\s*struct\s+([A-Za-z_][A-Za-z0-9_]*)", LSP_SYMBOL_KIND_STRUCT),
            (r"^\s*extern\s+pub\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)", LSP_SYMBOL_KIND_FUNCTION),
            (r"^\s*extern\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)", LSP_SYMBOL_KIND_FUNCTION),
            (r"^\s*pub\s+fn\s+([A-Za-z_][A-Za-z0-9_]*)", LSP_SYMBOL_KIND_FUNCTION),
            (r"^\s*fn\s+([A-Za-z_][A-Za-z0-9_]*)", LSP_SYMBOL_KIND_FUNCTION),
            (r"^\s*mod\s+([A-Za-z_][A-Za-z0-9_]*)\s*;", LSP_SYMBOL_KIND_MODULE),
        ]
        for pattern, kind in patterns:
            m = re.search(pattern, line)
            if not m:
                continue
            name = m.group(1)
            col = m.start(1)
            symbols.append(Symbol(name=name, kind=kind, uri=uri, line=line_no, character=col))
        return symbols

    def _word_at_position_with_index(
        self, text: str, line: int, character_utf16: int
    ) -> Tuple[str, int]:
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return "", -1
        line_text = lines[line]
        idx = self._utf16_to_index(line_text, character_utf16)
        if idx < 0 or idx > len(line_text):
            return "", -1

        left = idx
        while left > 0 and (line_text[left - 1].isalnum() or line_text[left - 1] == "_"):
            left -= 1
        right = idx
        while right < len(line_text) and (line_text[right].isalnum() or line_text[right] == "_"):
            right += 1
        return line_text[left:right], left

    def _extract_access_chain(self, text: str, line: int, word_start: int, word: str) -> List[str]:
        if word_start < 0:
            return []
        lines = text.splitlines()
        if line < 0 or line >= len(lines):
            return []
        line_text = lines[line]
        chain = [word]
        i = word_start - 1
        while True:
            while i >= 0 and line_text[i].isspace():
                i -= 1
            if i < 0 or line_text[i] != '.':
                break
            i -= 1
            while i >= 0 and line_text[i].isspace():
                i -= 1
            if i < 0:
                break
            end = i
            while i >= 0 and (line_text[i].isalnum() or line_text[i] == "_"):
                i -= 1
            base = line_text[i + 1:end + 1]
            if not base:
                break
            chain.insert(0, base)
        return chain if len(chain) > 1 else []

    def _resolve_chain_definition(
        self, uri: str, line: int, chain: List[str]
    ) -> Optional[Symbol]:
        if not chain:
            return None
        base = chain[0]
        cur_type = ""
        if base == "self":
            struct = self._find_enclosing_struct(uri, line)
            if struct:
                cur_type = struct.name
        else:
            fn = self._find_enclosing_function(uri, line)
            if fn:
                cur_type = fn.var_types.get(base, "")
        if not cur_type:
            return None

        for idx, field in enumerate(chain[1:], start=1):
            struct = self._find_struct_by_name(cur_type)
            if not struct:
                return None
            field_info = self._find_field_in_struct(struct, field)
            if not field_info:
                return None
            if idx == len(chain) - 1:
                return field_info.symbol
            cur_type = field_info.type_name
            if not cur_type:
                return None
        return None

    def _find_enclosing_struct(self, uri: str, line: int) -> Optional[StructInfo]:
        return self._find_enclosing_struct_from_list(self.structs_by_uri.get(uri, []), line)

    def _find_enclosing_struct_from_list(
        self, structs: List[StructInfo], line: int
    ) -> Optional[StructInfo]:
        for struct in structs:
            if struct.start_line <= line <= struct.end_line:
                return struct
        return None

    def _find_enclosing_function(self, uri: str, line: int) -> Optional[FunctionInfo]:
        functions = self.functions_by_uri.get(uri, [])
        for fn in functions:
            if fn.start_line <= line <= fn.end_line:
                return fn
        return None

    def _find_struct_by_name(self, name: str) -> Optional[StructInfo]:
        structs = self.structs_by_name.get(name, [])
        return structs[0] if structs else None

    def _find_field_in_struct(self, struct: StructInfo, field: str) -> Optional[FieldInfo]:
        if field in struct.fields:
            return struct.fields[field]
        if struct.base_name:
            base = self._find_struct_by_name(struct.base_name)
            if base:
                return self._find_field_in_struct(base, field)
        return None

    def _parse_params(self, params: str) -> Dict[str, str]:
        out: Dict[str, str] = {}
        for part in params.split(','):
            part = part.strip()
            if not part:
                continue
            m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.+)", part)
            if not m:
                continue
            name = m.group(1)
            type_name = self._normalize_type(m.group(2))
            if type_name:
                out[name] = type_name
        return out

    def _normalize_type(self, type_raw: str) -> str:
        if not type_raw:
            return ""
        type_raw = type_raw.strip()
        type_raw = type_raw.rstrip(";{")
        type_raw = type_raw.strip()
        if not type_raw:
            return ""
        # Strip generics like Box<Foo>
        if '<' in type_raw:
            type_raw = type_raw.split('<', 1)[0]
        # Take the first identifier
        m = WORD_RE.search(type_raw)
        return m.group(0) if m else ""

    def _find_function_return_type(self, name: str, uri: str) -> str:
        candidates = self.function_returns_by_name.get(name, [])
        for u, t in candidates:
            if u == uri:
                return t
        return candidates[0][1] if candidates else ""

    def _extract_position(self, params: dict) -> Tuple[Optional[str], int, int]:
        text_doc = params.get("textDocument", {})
        uri = text_doc.get("uri")
        position = params.get("position", {})
        line = position.get("line", 0)
        character = position.get("character", 0)
        return uri, line, character

    def _find_word_locations(self, uri: str, text: str, word: str) -> List[object]:
        locations: List[object] = []
        for line_no, line in enumerate(text.splitlines()):
            for m in WORD_RE.finditer(line):
                if m.group(0) != word:
                    continue
                locations.append(
                    {
                        "uri": uri,
                        "range": {
                            "start": {"line": line_no, "character": m.start()},
                            "end": {"line": line_no, "character": m.end()},
                        },
                    }
                )
        return locations

    def _is_definition(self, location: object) -> bool:
        if not isinstance(location, dict):
            return False
        uri = location.get("uri")
        if not uri:
            return False
        symbols = self.symbols_by_uri.get(uri, [])
        loc_range = location.get("range", {})
        start = loc_range.get("start", {})
        line = start.get("line")
        ch = start.get("character")
        for sym in symbols:
            if sym.line == line and sym.character == ch:
                return True
        return False

    def _symbol_to_location(self, sym: Symbol) -> object:
        return {
            "uri": sym.uri,
            "range": {
                "start": {"line": sym.line, "character": sym.character},
                "end": {"line": sym.line, "character": sym.character + len(sym.name)},
            },
        }

    def _symbol_to_document_symbol(self, sym: Symbol) -> object:
        return {
            "name": sym.name,
            "kind": sym.kind,
            "range": {
                "start": {"line": sym.line, "character": sym.character},
                "end": {"line": sym.line, "character": sym.character + len(sym.name)},
            },
            "selectionRange": {
                "start": {"line": sym.line, "character": sym.character},
                "end": {"line": sym.line, "character": sym.character + len(sym.name)},
            },
        }

    def _symbol_to_workspace_symbol(self, sym: Symbol) -> object:
        return {
            "name": sym.name,
            "kind": sym.kind,
            "location": self._symbol_to_location(sym),
        }

    def _path_to_location(self, path: str) -> object:
        return {
            "uri": self._path_to_uri(path),
            "range": {
                "start": {"line": 0, "character": 0},
                "end": {"line": 0, "character": 0},
            },
        }

    def _all_documents_with_disk(self) -> List[Tuple[str, str]]:
        out = []
        seen = set()
        for uri, text in self.documents.items():
            out.append((uri, text))
            seen.add(uri)
        if self.root_path:
            for dirpath, dirnames, filenames in os.walk(self.root_path):
                dirnames[:] = [
                    d for d in dirnames if d not in {".git", "build", "out", "dist"}
                ]
                for fname in filenames:
                    if not fname.endswith(MLANG_SOURCE_EXTS):
                        continue
                    path = os.path.join(dirpath, fname)
                    uri = self._path_to_uri(path)
                    if uri in seen:
                        continue
                    text = self._read_path_text(path)
                    if text is not None:
                        out.append((uri, text))
        return out

    def _strip_comments_and_strings(self, text: str) -> str:
        out = []
        i = 0
        state = "code"
        while i < len(text):
            c = text[i]
            if state == "code":
                if c == '"':
                    out.append(" ")
                    state = "string"
                    i += 1
                elif c == "/" and i + 1 < len(text) and text[i + 1] == "/":
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    state = "line_comment"
                elif c == "/" and i + 1 < len(text) and text[i + 1] == "*":
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    state = "block_comment"
                else:
                    out.append(c)
                    i += 1
            elif state == "string":
                if c == "\\" and i + 1 < len(text):
                    out.append(" ")
                    out.append(" ")
                    i += 2
                elif c == '"':
                    out.append(" ")
                    i += 1
                    state = "code"
                else:
                    out.append("\n" if c == "\n" else " ")
                    i += 1
            elif state == "line_comment":
                if c == "\n":
                    out.append("\n")
                    i += 1
                    state = "code"
                else:
                    out.append(" ")
                    i += 1
            elif state == "block_comment":
                if c == "*" and i + 1 < len(text) and text[i + 1] == "/":
                    out.append(" ")
                    out.append(" ")
                    i += 2
                    state = "code"
                else:
                    out.append("\n" if c == "\n" else " ")
                    i += 1
        return "".join(out)

    def _read_message(self) -> Optional[str]:
        header = b""
        while True:
            line = sys.stdin.buffer.readline()
            if not line:
                return None
            if line == b"\r\n":
                break
            header += line

        content_length = None
        for hdr_line in header.decode("ascii", errors="ignore").split("\r\n"):
            if hdr_line.lower().startswith("content-length:"):
                try:
                    content_length = int(hdr_line.split(":", 1)[1].strip())
                except ValueError:
                    content_length = None
                break

        if content_length is None:
            return None
        payload = sys.stdin.buffer.read(content_length)
        if not payload:
            return None
        return payload.decode("utf-8", errors="replace")

    def _send_response(self, msg_id: object, result: object) -> None:
        resp = {"jsonrpc": "2.0", "id": msg_id, "result": result}
        self._send_message(resp)

    def _send_message(self, msg: dict) -> None:
        data = json.dumps(msg, separators=(",", ":")).encode("utf-8")
        sys.stdout.buffer.write(
            b"Content-Length: " + str(len(data)).encode("ascii") + b"\r\n\r\n"
        )
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    def _read_path_text(self, path: str) -> Optional[str]:
        try:
            with open(path, "r", encoding="utf-8") as f:
                return f.read()
        except OSError:
            return None

    def _read_uri_text(self, uri: str) -> Optional[str]:
        path = self._uri_to_path(uri)
        return self._read_path_text(path)

    def _path_to_uri(self, path: str) -> str:
        path = os.path.abspath(path)
        path = path.replace(" ", "%20")
        return "file://" + path

    def _uri_to_path(self, uri: str) -> str:
        if uri.startswith("file://"):
            uri = uri[len("file://"):]
        return uri.replace("%20", " ")

    def _utf16_to_index(self, text: str, utf16_pos: int) -> int:
        count = 0
        for i, ch in enumerate(text):
            code = ord(ch)
            count += 2 if code > 0xFFFF else 1
            if count > utf16_pos:
                return i
            if count == utf16_pos:
                return i + 1
        return len(text)


def main() -> int:
    parser = argparse.ArgumentParser(description="Mlang LSP server")
    parser.add_argument("--stdio", action="store_true", help="Use stdio")
    args = parser.parse_args()
    if not args.stdio:
        sys.stderr.write("Only --stdio is supported.\n")
        return 2

    server = MlangLspServer()
    server.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
