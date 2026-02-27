#!/usr/bin/env python3
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.mlang_frontend.source_map import line_char_to_offset
from tools.mlang_frontend.workspace import WorkspaceIndex


class MlangFrontendWorkspaceTest(unittest.TestCase):
    def test_find_global_prefers_current_uri(self) -> None:
        ws = WorkspaceIndex()
        ws.open_document("file:///a.mlang", "fn entry() {}\n", 1)
        ws.open_document("file:///b.mlang", "fn entry() {}\n", 1)

        ref = ws.find_global("entry", prefer_uri="file:///b.mlang")
        self.assertIsNotNone(ref)
        assert ref is not None
        self.assertEqual(ref.uri, "file:///b.mlang")

    def test_global_references_across_documents(self) -> None:
        ws = WorkspaceIndex()
        ws.open_document("file:///defs.mlang", "fn helper() {}\n", 1)
        ws.open_document("file:///use.mlang", "fn main() {\n  helper()\n  helper()\n}\n", 1)

        occ = ws.references_for_global("helper")
        assert occ is not None
        starts = sorted((o.uri, o.start_offset, o.is_declaration) for o in occ)
        self.assertEqual(len(starts), 3)
        self.assertEqual(starts[0][0], "file:///defs.mlang")
        self.assertTrue(starts[0][2])

    def test_incremental_apply_changes_updates_text_and_version(self) -> None:
        ws = WorkspaceIndex()
        ws.open_document("file:///inc.mlang", "fn main() {\n  let valeu = 1\n}\n", 1)
        text = ws.apply_changes(
            "file:///inc.mlang",
            2,
            [
                {
                    "range": {
                        "start": {"line": 1, "character": 6},
                        "end": {"line": 1, "character": 11},
                    },
                    "text": "value",
                }
            ],
            line_char_to_offset,
        )
        self.assertIsNotNone(text)
        assert text is not None
        self.assertIn("value", text)
        self.assertEqual(ws.version("file:///inc.mlang"), 2)

    def test_stale_version_change_is_ignored(self) -> None:
        ws = WorkspaceIndex()
        ws.open_document("file:///stale.mlang", "let x = 1\n", 5)
        old = ws.text("file:///stale.mlang")
        text = ws.apply_changes(
            "file:///stale.mlang",
            4,
            [{"text": "let y = 2\n"}],
            line_char_to_offset,
        )
        self.assertEqual(text, old)
        self.assertEqual(ws.text("file:///stale.mlang"), old)
        self.assertEqual(ws.version("file:///stale.mlang"), 5)

    def test_search_globals_ranks_exact_prefix_then_contains(self) -> None:
        ws = WorkspaceIndex()
        ws.open_document("file:///a.mlang", "fn helper() {}\nfn help_me() {}\n", 1)
        ws.open_document("file:///b.mlang", "fn my_helper() {}\n", 1)
        out = ws.search_globals("help")
        names = [r.symbol.name for r in out]
        self.assertEqual(names[:3], ["help_me", "helper", "my_helper"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
