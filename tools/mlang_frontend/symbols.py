"""Symbol extraction and visibility queries for scaffold semantics."""

from __future__ import annotations

from dataclasses import dataclass
import re

from .parser import parse_module


@dataclass(frozen=True)
class Symbol:
    name: str
    kind: str
    start_offset: int
    end_offset: int
    scope_start: int
    scope_end: int
    container: str = ""


@dataclass(frozen=True)
class SymbolOccurrence:
    start_offset: int
    end_offset: int
    is_declaration: bool


def build_symbols(text: str) -> list[Symbol]:
    parsed = parse_module(text).module
    out: list[Symbol] = []
    file_end = len(text)

    for fn in parsed.functions:
        out.append(
            Symbol(
                name=fn.name,
                kind="function",
                start_offset=fn.start_offset,
                end_offset=fn.end_offset,
                scope_start=0,
                scope_end=file_end,
                container="",
            )
        )

    for let in parsed.top_level_lets:
        out.append(
            Symbol(
                name=let.name,
                kind="variable",
                start_offset=let.start_offset,
                end_offset=let.end_offset,
                scope_start=0,
                scope_end=file_end,
                container="",
            )
        )

    for fn in parsed.functions:
        for let in fn.lets:
            out.append(
                Symbol(
                    name=let.name,
                    kind="local_variable",
                    start_offset=let.start_offset,
                    end_offset=let.end_offset,
                    scope_start=fn.start_offset,
                    scope_end=fn.end_offset,
                    container=fn.name,
                )
            )

    return out


def visible_symbols(text: str, offset: int) -> list[Symbol]:
    offset = max(0, min(offset, len(text)))
    visible: list[Symbol] = []
    for sym in build_symbols(text):
        if not (sym.scope_start <= offset <= sym.scope_end):
            continue
        if sym.kind in {"variable", "local_variable"} and sym.start_offset > offset:
            continue
        visible.append(sym)
    return visible


def resolve_symbol(text: str, name: str, offset: int) -> Symbol | None:
    candidates = [s for s in visible_symbols(text, offset) if s.name == name]
    if not candidates:
        return None

    # Prefer local scope, then nearest declaration.
    candidates.sort(
        key=lambda s: (
            0 if s.kind == "local_variable" else 1,
            abs(offset - s.start_offset),
        )
    )
    return candidates[0]


def definition_symbol(text: str, offset: int) -> Symbol | None:
    token = _word_at(text, offset)
    if not token:
        return None
    return resolve_symbol(text, token, offset)


def references_for_symbol(text: str, symbol: Symbol) -> list[SymbolOccurrence]:
    out: list[SymbolOccurrence] = []

    # Declaration is always a reference anchor.
    out.append(
        SymbolOccurrence(
            start_offset=symbol.start_offset,
            end_offset=symbol.end_offset,
            is_declaration=True,
        )
    )

    pattern = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
    for m in pattern.finditer(text):
        if m.group(0) != symbol.name:
            continue
        start = m.start()
        if start == symbol.start_offset:
            continue
        resolved = resolve_symbol(text, symbol.name, start)
        if resolved is None:
            continue
        if not _same_symbol(resolved, symbol):
            continue
        out.append(
            SymbolOccurrence(
                start_offset=start,
                end_offset=m.end(),
                is_declaration=False,
            )
        )

    out.sort(key=lambda o: o.start_offset)
    return out


def _same_symbol(a: Symbol, b: Symbol) -> bool:
    return (
        a.name == b.name
        and a.kind == b.kind
        and a.start_offset == b.start_offset
        and a.container == b.container
    )


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

