"""Exercise build.sh with real CMake/configuration and a lightweight toolchain.

Run: python3 -m unittest discover -s tests -p test_build_script.py
"""

from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]


class BuildScriptTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shared = tempfile.TemporaryDirectory(prefix="mlang-config-test-")
        cls.config_binary = Path(cls.shared.name) / "mlang-config"
        try:
            subprocess.run(["c++", "-std=c++17", str(REPO / "tools/mlang_config.cpp"),
                            "-o", str(cls.config_binary)], check=True, capture_output=True)
        except Exception:
            cls.shared.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.shared.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mlang build test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.prefix = self.root / "installed"
        self.bin_dir = self.prefix / "bin"
        (self.root / "compiler.sh").write_text(
            '#!/bin/sh\nset -eu\nwhile [ "$#" -gt 0 ]; do\n'
            '  if [ "$1" = "-o" ]; then cp "$0" "$2"; exit 0; fi\n'
            '  shift\ndone\n')
        (self.root / "compiler.sh").chmod(0o755)
        (self.root / "CMakeLists.txt").write_text('''
cmake_minimum_required(VERSION 3.20)
project(BuildScriptFixture NONE)
add_custom_target(mlang-config
    COMMAND "${CMAKE_COMMAND}" -E copy "''' + str(self.config_binary) + '''" "${CMAKE_BINARY_DIR}/mlang-config")
add_custom_target(mlang
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/compiler.sh" "${CMAKE_BINARY_DIR}/mlang")
add_custom_target(mlang_std
    COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_BINARY_DIR}/libmlang_std.a")
install(PROGRAMS "${CMAKE_BINARY_DIR}/mlang" "${CMAKE_BINARY_DIR}/mlang-config" DESTINATION bin)
''')

    def run_build(self, *args, success=True):
        result = subprocess.run(
            ["sh", str(REPO / "build.sh"), "--build-dir", str(self.build), *args],
            cwd=self.root, input="", text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def install(self):
        return self.run_build("--install", "--prefix", str(self.prefix),
                              "--bin-dir", str(self.bin_dir), "--jobs", "2")

    def test_clean_workspace_builds_and_installs_all_tools(self):
        self.install()
        self.assertTrue((self.build / "mlang-config.conf").is_file())
        for name in ("mlang", "mlang-config", "mlangd-mla", "mlang-format",
                     "mlang-frontend-mla", "mlang-frontend", "mlangpkg"):
            self.assertTrue((self.bin_dir / name).is_file(), name)

    def test_existing_config_tool_creates_missing_default_config(self):
        self.build.mkdir()
        shutil.copy2(self.config_binary, self.build / "mlang-config")
        self.install()
        self.assertTrue((self.bin_dir / "mlangpkg").is_file())

    def test_existing_preferences_survive_rebuild(self):
        self.build.mkdir()
        subprocess.run([str(self.config_binary), "--build-dir", str(self.build),
                        "--mladbg", "off", "--unit-tests", "on", "--write"],
                       cwd=self.root, check=True, capture_output=True)
        self.install()
        config = (self.build / "mlang-config.conf").read_text()
        self.assertIn("install_mladbg=OFF", config)
        self.assertIn("run_unit_tests=ON", config)

    def test_explicit_missing_config_is_an_error(self):
        result = self.run_build("--config-file", "missing.conf", success=False)
        self.assertIn("missing config file: missing.conf", result.stderr)
        self.assertFalse(self.build.exists())

    @unittest.skipUnless(shutil.which("make"), "make is required for generator reuse test")
    def test_existing_cmake_generator_is_preserved(self):
        subprocess.run(["cmake", "-S", str(self.root), "-B", str(self.build),
                        "-G", "Unix Makefiles"], check=True, capture_output=True)
        self.install()
        self.assertIn("CMAKE_GENERATOR:INTERNAL=Unix Makefiles",
                      (self.build / "CMakeCache.txt").read_text())

    def run_bootstrap(self, answers, *args):
        # Save a real config without opening its menu, so input goes only to
        # bootstrap's confirmation prompts.
        wrapper = self.root / "config-wrapper.sh"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(str(self.config_binary)) +
                           " --build-dir " + shlex.quote(str(self.build)) + " --write\n")
        wrapper.chmod(0o755)
        cmake_file = self.root / "CMakeLists.txt"
        cmake_file.write_text(cmake_file.read_text().replace(str(self.config_binary), str(wrapper)))
        shutil.copy2(REPO / "build.sh", self.root / "build.sh")
        env = dict(os.environ, HOME=str(self.root / "home"))
        result = subprocess.run(
            ["sh", str(REPO / "bootstrap.sh"), "--build-dir", str(self.build), *args],
            cwd=self.root, input=answers, text=True, capture_output=True, env=env,
            timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_bootstrap_builds_only_config_before_confirmation(self):
        self.run_bootstrap("n\n", "--jobs", "2")
        self.assertTrue((self.build / "mlang-config").is_file())
        self.assertFalse((self.build / "mlang").exists())
        self.assertFalse((self.build / "libmlang_std.a").exists())
        self.assertFalse((self.build / "mlang-config.conf").exists())

    def test_bootstrap_enter_accepts_all_confirmations(self):
        result = self.run_bootstrap("\n\n\n")
        self.assertEqual(result.stdout.count("[Y/n]"), 3)
        self.assertTrue((self.build / "mlang-config.conf").is_file())
        self.assertTrue((self.root / "home/.local/bin/mlangpkg").is_file())

    def test_bootstrap_explicit_no_skips_full_build(self):
        self.run_bootstrap("Y\nN\n")
        self.assertTrue((self.build / "mlang-config.conf").is_file())
        self.assertFalse((self.build / "mlang").exists())

    def test_bootstrap_explicit_no_skips_install(self):
        self.run_bootstrap("\n\nn\n")
        self.assertTrue((self.build / "mlangpkg").is_file())
        self.assertFalse((self.root / "home/.local/bin").exists())

    def test_bootstrap_eof_skips_configuration(self):
        self.run_bootstrap("")
        self.assertTrue((self.build / "mlang-config").is_file())
        self.assertFalse((self.build / "mlang-config.conf").exists())


if __name__ == "__main__":
    unittest.main()
