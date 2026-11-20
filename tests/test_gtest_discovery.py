#!/usr/bin/env python3
"""Check that packaged CTest registrations survive a different CMake installation."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[1]


class GoogleTestDiscoveryTests(unittest.TestCase):
    def test_packaged_tree_does_not_need_builder_cmake_modules(self):
        configuration = (REPO / 'tests/CMakeLists.txt').read_text()
        registration = re.search(
            r'    gtest_discover_tests\(mla_tests\n.*?\n    \)', configuration, re.S)
        self.assertIsNotNone(registration, 'GoogleTest registration is missing')
        with tempfile.TemporaryDirectory(prefix='mlang-gtest-package-') as directory:
            root = Path(directory)
            build = root / 'build'
            (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(PackagedDiscovery LANGUAGES CXX)
enable_testing()
add_executable(mla_tests fixture.cpp)
include(GoogleTest)
''' + registration.group(0) + '\n')
            # Exercise real CMake discovery without fetching/building GoogleTest.
            (root / 'fixture.cpp').write_text('''#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    for(int i = 1; i < argc; ++i) {
        if(std::strcmp(argv[i], "--gtest_list_tests") == 0) {
            std::puts("PackagedDiscovery.\\n  Passes");
            return 0;
        }
        if(std::strcmp(argv[i], "--gtest_filter=PackagedDiscovery.Passes") == 0)
            return 0;
    }
    return 2;
}
''')
            for command in (['cmake', '-S', str(root), '-B', str(build)],
                            ['cmake', '--build', str(build)]):
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cache = (build / 'CMakeCache.txt').read_text()
            cmake_root = re.search(r'^CMAKE_ROOT:INTERNAL=(.+)$', cache, re.M)
            self.assertIsNotNone(cmake_root)
            # The test runner cannot access the producer's absolute CMake path.
            unavailable = str(root / 'unavailable-builder-cmake')
            for path in build.rglob('*.cmake'):
                source = path.read_text()
                path.write_text(source.replace(cmake_root.group(1), unavailable))
            result = subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure'],
                                    capture_output=True, text=True, timeout=60)
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertIn('PackagedDiscovery.Passes', output)
            self.assertIn('100% tests passed', output)


if __name__ == '__main__':
    unittest.main()
