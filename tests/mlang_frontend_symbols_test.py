#!/usr/bin/env python3
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.mlang_frontend.semantic import completion_symbols, hover_symbol
from tools.mlang_frontend.source_map import line_char_to_offset


class MlangFrontendSymbolsTest(unittest.TestCase):
    def test_completion_symbols_are_scope_aware(self) -> None:
        src = (
            "fn a() {\n"
            "  let inside_a = 1\n"
            "}\n"
            "fn b() {\n"
            "  let inside_b = 2\n"
            "}\n"
        )
        offset = line_char_to_offset(src, 1, 16)
        labels = {s.name for s in completion_symbols(src, offset)}
        self.assertIn("inside_a", labels)
        self.assertNotIn("inside_b", labels)

    def test_completion_symbols_honor_declaration_order(self) -> None:
        src = "let top = 1\nfn main() {\n  let local = top\n}\n"
        before_top = line_char_to_offset(src, 0, 0)
        after_top = line_char_to_offset(src, 1, 0)
        labels_before = {s.name for s in completion_symbols(src, before_top)}
        labels_after = {s.name for s in completion_symbols(src, after_top)}
        self.assertNotIn("top", labels_before)
        self.assertIn("top", labels_after)

    def test_hover_prefers_local_over_global(self) -> None:
        src = "let value = 1\nfn main() {\n  let value = 2\n  value\n}\n"
        offset = line_char_to_offset(src, 3, 7)
        sym = hover_symbol(src, "value", offset)
        self.assertIsNotNone(sym)
        assert sym is not None
        self.assertEqual(sym.kind, "local_variable")
        self.assertEqual(sym.container, "main")


if __name__ == "__main__":
    unittest.main(verbosity=2)
