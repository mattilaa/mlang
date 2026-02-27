"""Workspace index for scaffold cross-file symbol queries."""

from __future__ import annotations

from dataclasses import dataclass
import re
from typing import Callable

from .symbols import Symbol, build_symbols


@dataclass(frozen=True)
class GlobalSymbolRef:
    uri: str
    symbol: Symbol


@dataclass(frozen=True)
class WorkspaceOccurrence:
    uri: str
    start_offset: int
    end_offset: int
    is_declaration: bool


@dataclass
class WorkspaceDocument:
    text: str
    version: int


class WorkspaceIndex:
    def __init__(self) -> None:
        self._docs: dict[str, WorkspaceDocument] = {}
        self._globals_by_uri: dict[str, list[Symbol]] = {}

    def open_document(self, uri: str, text: str, version: int) -> None:
        self._docs[uri] = WorkspaceDocument(text=text, version=version)
        self._reindex_globals(uri)

    def apply_changes(
        self,
        uri: str,
        new_version: int,
        changes: list[dict],
        line_char_to_offset: Callable[[str, int, int], int],
    ) -> str | None:
        doc = self._docs.get(uri)
        if doc is None:
            return None
        if new_version <= doc.version:
            return doc.text

        text = doc.text
        for change in changes:
            text = self._apply_single_change(text, change, line_char_to_offset)

        doc.text = text
        doc.version = new_version
        self._reindex_globals(uri)
        return text

    def text(self, uri: str) -> str | None:
        doc = self._docs.get(uri)
        return None if doc is None else doc.text

    def version(self, uri: str) -> int | None:
        doc = self._docs.get(uri)
        return None if doc is None else doc.version

    def update_document(self, uri: str, text: str) -> None:
        prev = self._docs.get(uri)
        version = 0 if prev is None else prev.version
        self._docs[uri] = WorkspaceDocument(text=text, version=version)
        self._reindex_globals(uri)

    def remove_document(self, uri: str) -> None:
        self._docs.pop(uri, None)
        self._globals_by_uri.pop(uri, None)

    def find_global(self, name: str, prefer_uri: str = "") -> GlobalSymbolRef | None:
        if not name:
            return None

        if prefer_uri:
            for sym in self._globals_by_uri.get(prefer_uri, []):
                if sym.name == name:
                    return GlobalSymbolRef(uri=prefer_uri, symbol=sym)

        for uri, symbols in self._globals_by_uri.items():
            if uri == prefer_uri:
                continue
            for sym in symbols:
                if sym.name == name:
                    return GlobalSymbolRef(uri=uri, symbol=sym)
        return None

    def search_globals(self, query: str, limit: int = 200) -> list[GlobalSymbolRef]:
        q = (query or "").strip().lower()
        out: list[GlobalSymbolRef] = []

        for uri, symbols in self._globals_by_uri.items():
            for sym in symbols:
                name_l = sym.name.lower()
                if q and q not in name_l:
                    continue
                out.append(GlobalSymbolRef(uri=uri, symbol=sym))

        out.sort(
            key=lambda it: (
                0 if it.symbol.name.lower() == q else 1,
                0 if it.symbol.name.lower().startswith(q) else 1,
                it.symbol.name,
                it.uri,
            )
        )
        return out[: max(0, limit)]

    def references_for_global(
        self,
        name: str,
        is_cancelled: Callable[[], bool] | None = None,
        on_progress: Callable[[int, int], None] | None = None,
    ) -> list[WorkspaceOccurrence] | None:
        if not name:
            return []

        out: list[WorkspaceOccurrence] = []
        pattern = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

        items = list(self._docs.items())
        total = len(items)
        for idx, (uri, doc) in enumerate(items, start=1):
            if is_cancelled and is_cancelled():
                return None

            text = doc.text
            decl_starts = {
                sym.start_offset
                for sym in self._globals_by_uri.get(uri, [])
                if sym.name == name
            }
            for m in pattern.finditer(text):
                if m.group(0) != name:
                    continue
                out.append(
                    WorkspaceOccurrence(
                        uri=uri,
                        start_offset=m.start(),
                        end_offset=m.end(),
                        is_declaration=m.start() in decl_starts,
                    )
                )

            if on_progress:
                on_progress(idx, total)

        out.sort(key=lambda o: (o.uri, o.start_offset))
        return out

    def _reindex_globals(self, uri: str) -> None:
        doc = self._docs.get(uri)
        if doc is None:
            self._globals_by_uri.pop(uri, None)
            return
        globals_only = [
            s for s in build_symbols(doc.text) if s.kind in {"function", "variable"}
        ]
        self._globals_by_uri[uri] = globals_only

    @staticmethod
    def _apply_single_change(
        text: str,
        change: dict,
        line_char_to_offset: Callable[[str, int, int], int],
    ) -> str:
        if "range" not in change:
            return change.get("text", "")

        rng = change["range"]
        start = rng.get("start", {})
        end = rng.get("end", {})
        start_off = line_char_to_offset(
            text, int(start.get("line", 0)), int(start.get("character", 0))
        )
        end_off = line_char_to_offset(
            text, int(end.get("line", 0)), int(end.get("character", 0))
        )
        if end_off < start_off:
            end_off = start_off
        return text[:start_off] + change.get("text", "") + text[end_off:]

