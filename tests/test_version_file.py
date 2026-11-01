"""Check version.txt parsing and automatic CMake reconfiguration."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]


class VersionFileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mlang-version-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        (self.root / "cmake").mkdir()
        shutil.copy2(REPO / "cmake/MLangVersion.cmake", self.root / "cmake/MLangVersion.cmake")
        (self.root / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.20)
include(cmake/MLangVersion.cmake)
project(VersionFixture VERSION "${MLANG_VERSION_MAJOR}.${MLANG_VERSION_MINOR}.${MLANG_VERSION_BUGFIX}" LANGUAGES NONE)
file(WRITE "${CMAKE_BINARY_DIR}/reported-version.txt" "${PROJECT_VERSION}\\n")
add_custom_target(version COMMAND "${CMAKE_COMMAND}" -E cat "${CMAKE_BINARY_DIR}/reported-version.txt")
''')

    def configure(self):
        return subprocess.run(["cmake", "-S", str(self.root), "-B", str(self.build)],
                              text=True, capture_output=True)

    def test_reads_public_version(self):
        (self.root / "version.txt").write_text("0.4.4\n")
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.build / "reported-version.txt").read_text(), "0.4.4\n")

    def test_build_reconfigures_when_version_changes(self):
        (self.root / "version.txt").write_text("0.4.4\n")
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (self.root / "version.txt").write_text("0.4.5\n")
        result = subprocess.run(["cmake", "--build", str(self.build), "--target", "version"],
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.build / "reported-version.txt").read_text(), "0.4.5\n")

    def test_rejects_invalid_version(self):
        for version in ("0.4", "v0.4.4", "0.4.04", "0.4.4.1", "0.4.4-rc1", ""):
            with self.subTest(version=version):
                (self.root / "version.txt").write_text(version + "\n")
                result = self.configure()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("version.txt must contain MAJOR.MINOR.BUGFIX", result.stderr)


if __name__ == "__main__":
    unittest.main()
