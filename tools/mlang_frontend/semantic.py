"""Small semantic helpers for LSP hover/completion/queries."""

from __future__ import annotations

from .symbols import (
    Symbol,
    SymbolOccurrence,
    definition_symbol,
    references_for_symbol,
    resolve_symbol,
    visible_symbols,
)


def completion_symbols(text: str, offset: int) -> list[Symbol]:
    symbols = visible_symbols(text, offset)
    # Keep declaration order while de-duplicating by name with local-scope priority.
    by_name: dict[str, Symbol] = {}
    for sym in symbols:
        prev = by_name.get(sym.name)
        if prev is None:
            by_name[sym.name] = sym
            continue
        if prev.kind != "local_variable" and sym.kind == "local_variable":
            by_name[sym.name] = sym
    return list(by_name.values())


def hover_symbol(text: str, name: str, offset: int) -> Symbol | None:
    return resolve_symbol(text, name, offset)


def definition_at(text: str, offset: int) -> Symbol | None:
    return definition_symbol(text, offset)


def references_at(text: str, offset: int) -> list[SymbolOccurrence]:
    sym = definition_symbol(text, offset)
    if sym is None:
        return []
    return references_for_symbol(text, sym)

