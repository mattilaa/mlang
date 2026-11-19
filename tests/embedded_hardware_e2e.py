#!/usr/bin/env python3
"""IR and diagnostics for unsafe embedded hardware primitives."""
import argparse
from pathlib import Path
import sys
import unittest
from freestanding_data_e2e import FreestandingDataTests


class EmbeddedHardwareTests(FreestandingDataTests):
    # Reuse compiler setup without rerunning the freestanding test suite here.
    test_static_aggregates_have_no_heap_or_constructors = None
    test_rejects_invalid_or_unsafe_operations = None
    test_runtime_bounds_checks_abort = None
    test_imported_globals_are_shared_across_diamond_imports = None

    def test_hardware_ir_on_both_targets(self):
        source = '''
        #[align(16)] var dma: fixedarray<u8, 64>;
        pub fn io(address: u64, value: u32) -> u32 {
            unsafe {
                let p: ptr<u32> = ptr_from_address<u32>(address);
                volatile_store(p, value);
                compiler_barrier();
                return volatile_load(p) + volatile_load(p);
            }
        }
        pub fn storage() -> u64 { unsafe { return ptr_address(&mut dma[0]); } }
        '''
        for target in ("x86", "x64", "aarch64"):
            for optimization in ("-O0", "-O2"):
                with self.subTest(target=target, optimization=optimization):
                    result, output = self.compile(source.replace("address: u64", "address: u32") if target == "x86" else source, "--target-arch", target, optimization)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    ir = output.read_text()
                    self.assertIn("store volatile i32", ir)
                    self.assertEqual(ir.count("load volatile i32"), 2)
                    self.assertIn("inttoptr", ir)
                    self.assertIn("ptrtoint", ir)
                    self.assertRegex(ir, r"@__mlang_global_dma[^\n]*align 16")
                    self.assertIn('asm sideeffect "", "~{memory}"', ir)
                    self.assertNotRegex(ir, r"\bcall\b[^\n]*@(?:malloc|calloc|realloc)\(")

    def test_explicit_registers_and_volatile_memory_clobber(self):
        source = '''
        pub fn exit_host(block: ptr<u64>) -> void {
            unsafe { asm volatile aarch64(void, "hlt #0xf000", 24u64, block : "{x0},{x1}"); }
        }
        pub fn barrier() -> void { asm volatile aarch64(void, "dmb sy"); }
        pub fn counter() -> u64 {
            unsafe { return asm volatile aarch64(u64, "mrs $0, cntpct_el0" : "=r"); }
        }
        '''
        result, output = self.compile(source, "--target-arch", "aarch64", "-O2")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        ir = output.read_text()
        self.assertIn('"{x0},{x1},~{memory}"', ir)
        self.assertIn('"dmb sy", "~{memory}"', ir)
        self.assertIn('"mrs $0, cntpct_el0", "=r,~{memory}"', ir)

    def test_rejects_invalid_hardware_operations(self):
        cases = [
            ('fn f(a: u64) -> ptr<u32> { return ptr_from_address<u32>(a); }', 'unsafe'),
            ('fn f(p: ptr<u32>) -> u32 { return volatile_load(p); }', 'unsafe'),
            ('fn f(p: ptr<u32>) -> void { volatile_store(p, 7u32); }', 'unsafe'),
            ('fn f(p: ptr<u32>) -> u64 { return ptr_address(p); }', 'unsafe'),
            ('fn f() -> void { compiler_barrier(); }', 'unsafe'),
            ('fn f(a: u32) -> ptr<u32> { unsafe { return ptr_from_address<u32>(a); } }', 'pointer width'),
            ('fn f() -> void { unsafe { volatile_load(7u64); } }', 'typed raw pointer'),
            ('fn f(p: ptr<f64>) -> f64 { unsafe { return volatile_load(p); } }', 'integer element'),
            ('fn f(p: ptr<u32>) -> void { unsafe { volatile_store(p, 1.5); } }', 'must match'),
            ('fn main() -> i32 { var x: u32 = 0u32; unsafe { volatile_store(&x, 7u32); } return 0; }', 'shared borrow'),
            ('fn write(p: ptr<u32>) -> void { unsafe { volatile_store(p, 7u32); } } fn main() -> i32 { var x: u32 = 0u32; write(&x); return 0; }', 'shared borrow'),
            ('#[align(0)] var a: fixedarray<u8, 2>;', 'power of two'),
            ('#[align(3)] var a: fixedarray<u8, 2>;', 'power of two'),
            ('fn f() -> void { #[align(3)] var a: u32 = 0u32; }', 'power of two'),
            ('#[align(1073741824)] var a: u32;', 'power of two'),
            ('fn f() -> void { unsafe { asm volatile(void, "" : "=r"); } }', 'constraints'),
            ('fn f() -> void { asm volatile(void, "" : "~{memory}"); }', 'unsafe'),
            ('fn f() -> void { unsafe { volatile_store(); } }', 'argument count'),
            ('fn f(p: ptr<u32>) -> void { unsafe { asm volatile(void, "", p : "*m"); } }', 'constraints'),
            ('fn f(p: ptr<u32>) -> void { unsafe { asm volatile(void, "", p : "m"); } }', 'constraints'),
            ('fn f() -> void { #[align(16)] cexpr x: i32 = 1; }', 'storage'),
            ('fn f(a: u64) -> void { unsafe { ptr_from_address<u32, u8>(a); } }', 'one element type'),
        ]
        for source, diagnostic in cases:
            with self.subTest(source=source):
                result, _ = self.compile(source, "-O0")
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(diagnostic, result.stdout + result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path("build/mlang"))
    parser.add_argument("--stdlib", type=Path, default=Path("build"))
    args = parser.parse_args()
    EmbeddedHardwareTests.compiler = args.compiler.resolve()
    EmbeddedHardwareTests.stdlib = args.stdlib.resolve()
    # Importing a TestCase otherwise makes unittest discover the base class too.
    del FreestandingDataTests
    unittest.main(argv=[sys.argv[0]], verbosity=2)
