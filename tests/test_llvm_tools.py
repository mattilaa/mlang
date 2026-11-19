"""Exercise LLVM/Clang selection without requiring multiple installed LLVMs."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest


MODULE = Path(__file__).resolve().parents[1] / "cmake/MLangLLVMTools.cmake"


@unittest.skipUnless(os.name == "posix", "fake compiler executables use POSIX shell")
class LLVMToolsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mlang llvm tools ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def clang(self, directory, name, version, exit_code=0):
        directory.mkdir(parents=True, exist_ok=True)
        executable = directory / name
        executable.write_text(f'#!/bin/sh\nprintf "%s\\n" "Ubuntu clang version {version}.0.0"\nexit {exit_code}\n')
        executable.chmod(0o755)
        return executable

    def configure(self, major, tools, override=None, path=None):
        script = self.root / "test.cmake"
        override_line = f'set(MLANG_CLANGXX_EXE [[{override}]])' if override else ''
        script.write_text(f'''cmake_minimum_required(VERSION 3.20)
set(LLVM_PACKAGE_VERSION "{major}.1.0")
set(LLVM_TOOLS_BINARY_DIR [[{tools}]])
{override_line}
include([[{MODULE}]])
file(WRITE [[{self.root / "selected"}]] "${{MLANG_CLANGXX_EXE}}")
''')
        env = os.environ.copy()
        if path:
            env["PATH"] = str(path) + os.pathsep + env["PATH"]
        return subprocess.run(["cmake", "-P", str(script)], env=env,
                              capture_output=True, text=True, timeout=30)

    def test_prefers_linked_llvm_tools_over_old_clang_on_path(self):
        tools = self.root / "llvm22/bin"
        expected = self.clang(tools, "clang++", 22)
        old_tools = self.root / "old/bin"
        self.clang(old_tools, "clang++-17", 17)
        self.clang(old_tools, "clang++", 17)
        result = self.configure(22, tools, path=old_tools)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "selected").read_text(), str(expected))

    def test_selects_llvm17_when_host_path_contains_newer_clang(self):
        tools = self.root / "llvm17/bin"
        expected = self.clang(tools, "clang++-17", 17)
        host = self.root / "host/bin"
        self.clang(host, "clang++", 22)
        result = self.configure(17, tools, path=host)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "selected").read_text(), str(expected))

    def test_rejects_old_cached_clang_with_actionable_diagnostic(self):
        old = self.clang(self.root / "old", "clang++-17", 17)
        result = self.configure(22, self.root / "llvm22/bin", override=old)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("LLVM IR toolchain mismatch", result.stderr)
        self.assertIn("-UMLANG_CLANGXX_EXE", result.stderr)
        self.assertIn("-DLLVM_DIR", result.stderr)
        self.assertFalse((self.root / "selected").exists())

    def test_accepts_explicit_matching_compiler(self):
        clang = self.clang(self.root / "custom/bin", "clang++", 17)
        result = self.configure(17, self.root / "llvm17/bin", override=clang)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_nonworking_compiler(self):
        clang = self.clang(self.root / "broken", "clang++", 22, exit_code=3)
        result = self.configure(22, self.root / "llvm22/bin", override=clang)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot determine the LLVM version", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
