#!/usr/bin/env python3
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.mlang_frontend.workspace import WorkspaceIndex


class MlangFrontendWorkspaceTest(unittest.TestCase):
    def test_find_global_prefers_current_uri(self) -> None:
        ws = WorkspaceIndex()
        ws.update_document("file:///a.mlang", "fn entry() {}\n")
        ws.update_document("file:///b.mlang", "fn entry() {}\n")

        ref = ws.find_global("entry", prefer_uri="file:///b.mlang")
        self.assertIsNotNone(ref)
        assert ref is not None
        self.assertEqual(ref.uri, "file:///b.mlang")

    def test_global_references_across_documents(self) -> None:
        ws = WorkspaceIndex()
        ws.update_document(
            "file:///defs.mlang",
            "fn helper() {}\n",
        )
        ws.update_document(
            "file:///use.mlang",
            "fn main() {\n  helper()\n  helper()\n}\n",
        )

        occ = ws.references_for_global("helper")
        starts = sorted((o.uri, o.start_offset, o.is_declaration) for o in occ)
        self.assertEqual(len(starts), 3)
        self.assertEqual(starts[0][0], "file:///defs.mlang")
        self.assertTrue(starts[0][2])


if __name__ == "__main__":
    unittest.main(verbosity=2)
