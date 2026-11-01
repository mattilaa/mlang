"""Run with: python3 -m unittest discover -s tests -p test_bump_release_version.py"""

import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "bump_release_version", Path(__file__).resolve().parents[1] / "scripts/bump_release_version.py")
bump_script = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bump_script)


class BumpReleaseVersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "version.txt").write_text("0.4.9\n")
        self.git("init", "-q")
        self.git("config", "user.email", "test@example.com")
        self.git("config", "user.name", "Test")
        self.commit()
        self.git("tag", "v0.4.9")
        self.release = dict(tagName="v0.4.9", isDraft=False, isPrerelease=False,
                            publishedAt="2026-11-01T00:00:00Z")
        real_run = bump_script.run

        def run(*args, cwd):
            if args[0] == "gh":
                return json.dumps(self.release)
            return real_run(*args, cwd=cwd)

        mock = patch.object(bump_script, "run", side_effect=run)
        mock.start()
        self.addCleanup(mock.stop)

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.root, text=True,
                                       stderr=subprocess.PIPE).strip()

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-qm", "Fixture")

    def snapshot(self):
        return {name: (self.root / name).read_bytes() for name in bump_script.VERSION_FILES}

    def test_confirmation_and_patch_carry(self):
        with patch("builtins.input", return_value="yes"):
            self.assertTrue(bump_script.bump(self.root, "v0.4.9"))
        self.assertEqual((self.root / "version.txt").read_text(), "0.4.10\n")

    def test_decline_or_eof_preserves_files(self):
        before = self.snapshot()
        for answer in ("no", "", EOFError()):
            kwargs = {"side_effect": answer} if isinstance(answer, Exception) else {"return_value": answer}
            with patch("builtins.input", **kwargs):
                self.assertFalse(bump_script.bump(self.root, "v0.4.9"))
            self.assertEqual(before, self.snapshot())

    def test_rerun_after_committed_bump_is_noop(self):
        bump_script.bump(self.root, "v0.4.9", True)
        self.commit()
        before = self.snapshot()
        self.assertFalse(bump_script.bump(self.root, "v0.4.9", True))
        self.assertEqual(before, self.snapshot())

    def test_unpublished_or_prerelease_rejected(self):
        before = self.snapshot()
        for field, value in (("isDraft", True), ("isPrerelease", True), ("publishedAt", None)):
            original = self.release[field]
            self.release[field] = value
            with self.assertRaises(ValueError):
                bump_script.bump(self.root, "v0.4.9", True)
            self.release[field] = original
            self.assertEqual(before, self.snapshot())

    def test_invalid_or_missing_tag_rejected(self):
        for tag in ("v0.4", "0.4.9", "v0.04.9", "v0.4.9-rc1", "v0.4.8"):
            with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                bump_script.bump(self.root, tag, True)

    def test_dirty_file_rejected(self):
        (self.root / "version.txt").write_text("local edits\n")
        before = self.snapshot()
        with self.assertRaises(ValueError):
            bump_script.bump(self.root, "v0.4.9", True)
        self.assertEqual(before, self.snapshot())

    def test_invalid_source_version_rejected(self):
        for version in ("0.4", "v0.4.9", "0.4.09", "0.4.9.1", "0.4.9-rc1", ""):
            (self.root / "version.txt").write_text(version + "\n")
            self.commit()
            before = self.snapshot()
            with self.assertRaises(ValueError):
                bump_script.bump(self.root, "v0.4.9", True)
            self.assertEqual(before, self.snapshot())

    def test_source_older_than_release_rejected(self):
        self.git("tag", "v0.4.10")
        self.release["tagName"] = "v0.4.10"
        with self.assertRaises(ValueError):
            bump_script.bump(self.root, "v0.4.10", True)

    def test_tag_outside_checkout_history_rejected(self):
        (self.root / "extra").write_text("another commit")
        self.commit()
        self.git("tag", "v0.4.10")
        self.git("checkout", "--detach", "-q", "v0.4.9")
        with self.assertRaises(subprocess.CalledProcessError):
            bump_script.bump(self.root, "v0.4.10", True)


if __name__ == "__main__":
    unittest.main()
