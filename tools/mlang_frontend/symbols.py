"""Symbol extraction and visibility queries for scaffold semantics."""

from __future__ import annotations

from dataclasses import dataclass

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

