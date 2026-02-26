"""Minimal frontend utilities used by the mlang LSP scaffold."""

from .parser import collect_symbols, parse_module
from .semantic import completion_symbols, definition_at, hover_symbol, references_at
from .workspace import WorkspaceIndex

__all__ = [
    "collect_symbols",
    "completion_symbols",
    "definition_at",
    "hover_symbol",
    "parse_module",
    "references_at",
    "WorkspaceIndex",
]
