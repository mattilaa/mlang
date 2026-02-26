"""Workspace index for scaffold cross-file symbol queries."""

from __future__ import annotations

from dataclasses import dataclass
import re

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


class WorkspaceIndex:
    def __init__(self) -> None:
        self._docs: dict[str, str] = {}
        self._globals_by_uri: dict[str, list[Symbol]] = {}

    def update_document(self, uri: str, text: str) -> None:
        self._docs[uri] = text
        globals_only = [
            s for s in build_symbols(text) if s.kind in {"function", "variable"}
        ]
        self._globals_by_uri[uri] = globals_only

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

    def references_for_global(self, name: str) -> list[WorkspaceOccurrence]:
        if not name:
            return []

        out: list[WorkspaceOccurrence] = []
        pattern = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

        for uri, text in self._docs.items():
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

        out.sort(key=lambda o: (o.uri, o.start_offset))
        return out

