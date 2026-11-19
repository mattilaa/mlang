#!/usr/bin/env python3
"""Compiler diagnostics, bounds failures and IR checks for freestanding data."""
import argparse
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import unittest


class FreestandingDataTests(unittest.TestCase):
    compiler = None
    stdlib = None

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="mlang-freestanding-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def compile(self, source, *options, link=False):
        source_file = self.root / "test.mla"
        source_file.write_text(source)
        output = self.root / ("test" if link else "test.ll")
        command = [str(self.compiler), str(source_file), "-o", str(output), *options]
        command += ["-L", str(self.stdlib), "-lmlang_std"] if link else ["-emit-llvm"]
        result = subprocess.run(command, cwd=self.root, capture_output=True, text=True, timeout=60)
        return result, output

    def test_static_aggregates_have_no_heap_or_constructors(self):
        source = """
        struct Node { var table: fixedarray<i32, 3>; var other: ptr<Node>; };
        var first: Node = Node { table: [7, 8, 9], other: &mut second };
        var second: Node = Node { table: [10, 11, 12], other: &mut first };
        fn make(n: i32) -> fixedarray<i32, 3> { return [n, n + 1, n + 2]; }
        pub fn read(i: i32) -> i32 {
            let local: fixedarray<i32, 3> = make(i);
            return local[i] + first.table[i];
        }
        """
        for architecture in ("x64", "aarch64"):
            for optimization in ("-O0", "-O2"):
                with self.subTest(architecture=architecture, optimization=optimization):
                    result, output = self.compile(source, "--target-arch", architecture, optimization)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    ir = output.read_text()
                    self.assertNotRegex(ir, r"\bcall\b[^\n]*@(?:malloc|calloc|realloc)\(")
                    self.assertNotIn("llvm.global_ctors", ir)
                    self.assertIn("[3 x i32]", ir)
                    self.assertRegex(ir, r"@__mlang_global_first.*@__mlang_global_second")

    def test_imported_globals_are_shared_across_diamond_imports(self):
        (self.root / "storage.mla").write_text("""
            #[align(16)] var words: fixedarray<i32, 3> = [7, 8, 9];
            pub fn bump() -> i32 { words[1] += 1; return words[1]; }
        """)
        (self.root / "facade.mla").write_text("""
            mod storage; use storage::*;
            pub fn bump_again() -> i32 { return bump(); }
        """)
        source = """
            mod storage; use storage::*;
            mod facade; use facade::*;
            fn main() -> i32 {
                if bump() != 9 { return 1; }
                return bump_again() == 10 ? 0 : 2;
            }
        """
        result, output = self.compile(source, "-O0")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ir = output.read_text()
        self.assertEqual(len(re.findall(r"^@__mlang_global_words =", ir, re.M)), 1)
        self.assertRegex(ir, r"@__mlang_global_words[^\n]*align 16")
        result, output = self.compile(source, "-O2", link=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = subprocess.run([str(output)], cwd=self.root, capture_output=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_rejects_invalid_or_unsafe_operations(self):
        cases = {
            "raw pointer offset needs unsafe": (
                "fn f(p: ptr<i32>) -> ptr<i32> { return p + 1; }", "unsafe"),
            "raw pointer indexing needs unsafe": (
                "fn f(p: ptr<i32>) -> i32 { return p[0]; }", "unsafe"),
            "floating pointer offset": (
                "fn f(p: ptr<i32>) -> ptr<i32> { unsafe { return p + 1.5; } }", "integer offset"),
            "void pointer indexing": (
                "fn f(p: ptr<void>) -> void { unsafe { p[0]; } }", "sized"),
            "zero-size pointer difference": (
                "fn f(p: ptr<fixedarray<i32, 0>>) -> i64 { unsafe { return p - p; } }", "sized"),
            "mismatched pointer difference": (
                "fn f(p: ptr<i32>, q: ptr<u8>) -> i64 { unsafe { return p - q; } }", "matching"),
            "shared borrow indexed write": (
                "fn main() -> i32 { var x: i32 = 1; let p: ptr<i32> = &x; unsafe { p[0] = 2; } return 0; }", "shared borrow"),
            "shared borrow offset write": (
                "fn main() -> i32 { var x: i32 = 1; let p: ptr<i32> = &x; unsafe { *(p + 0) = 2; } return 0; }", "shared borrow"),
            "shared derived pointer call": (
                "fn write(p: ptr<i32>) -> void { unsafe { p[0] = 2; } } fn main() -> i32 { var x: i32 = 1; let p: ptr<i32> = &x; unsafe { write(p + 0); } return 0; }", "shared borrow"),
            "derived shared alias write": (
                "fn main() -> i32 { var x: i32 = 1; let p: ptr<i32> = &x; unsafe { let q: ptr<i32> = p + 0; q[0] = 2; } return 0; }", "shared borrow"),
            "forwarded offset local escape": (
                "fn next(p: ptr<i32>) -> ptr<i32> { unsafe { return p + 0; } } fn f() -> ptr<i32> { var x: i32 = 1; unsafe { return next(&mut x); } }", "local"),
            "offset local pointer escape": (
                "fn f() -> ptr<i32> { var x: i32 = 1; let p: ptr<i32> = &mut x; unsafe { return p + 0; } }", "local"),
            "inline array borrow escape": (
                "fn f() -> fixedarray<ptr<i32>, 1> { var x: i32 = 1; return [&mut x]; }", "local"),
            "immutable inline array": (
                "fn f() -> i32 { let a: fixedarray<i32, 2> = [1, 2]; a[0] = 3; return 0; }", "immutable"),
            "inline array too many values": (
                "fn f() -> i32 { let a: fixedarray<i32, 2> = [1, 2, 3]; return 0; }", "extent"),
            "constant inline array out of bounds": (
                "fn f() -> i32 { let a: fixedarray<i32, 2> = [1, 2]; return a[2]; }", "out of bounds"),
            "inline array cannot resize": (
                "fn f() -> i32 { var a: fixedarray<i32, 2> = [1]; a.push(2); return 0; }", "fixed-size storage"),
            "string storage is immutable": (
                "fn f() -> i32 { var a: str8 = \"abc\"; a[0] = 65; return 0; }", "mutable"),
            "nonconstant global initializer": (
                "fn value() -> i32 { return 7; } struct S { var x: i32; }; var s: S = S { x: value() };", "constant"),
            "unknown global initializer field": (
                "struct S { var x: i32; }; var s: S = S { missing: 1 };", "unknown"),
        }
        for name, (source, diagnostic) in cases.items():
            with self.subTest(name=name):
                result, _ = self.compile(source, "-O0")
                self.assertNotEqual(result.returncode, 0, name + " unexpectedly compiled")
                self.assertIn(diagnostic, result.stdout + result.stderr)

    @unittest.skipUnless(os.name == "posix", "checks the native abort signal")
    def test_runtime_bounds_checks_abort(self):
        sources = [
            'fn at(s: str8, i: i32) -> i32 { return i32(s[i]); } fn main() -> i32 { return at("abc", 4); }',
            'fn at(s: str8, i: i32) -> i32 { return i32(s[i]); } fn main() -> i32 { return at("abc", -1); }',
            'fn at(a: fixedarray<i32, 2>, i: i32) -> i32 { return a[i]; } fn main() -> i32 { return at([1, 2], 2); }',
        ]
        for source in sources:
            for optimization in ("-O0", "-O2"):
                with self.subTest(source=source, optimization=optimization):
                    result, output = self.compile(source, optimization, link=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    run = subprocess.run([str(output)], cwd=self.root, capture_output=True, timeout=10)
                    self.assertEqual(run.returncode, -signal.SIGABRT)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build/mlang"))
    parser.add_argument("--stdlib", type=Path, default=Path("build"))
    arguments = parser.parse_args()
    FreestandingDataTests.compiler = arguments.compiler.resolve()
    FreestandingDataTests.stdlib = arguments.stdlib.resolve()
    unittest.main(argv=[sys.argv[0]], verbosity=2)
