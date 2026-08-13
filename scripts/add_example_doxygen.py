#!/usr/bin/env python3
"""Prepend Doxygen @file/@ingroup examples/@brief headers to example .mla files.

Curated brief mapping so each example has a human description of what it
demonstrates. Run from the repository root.
"""
from __future__ import annotations

import os
import sys

# path (relative to repo root) -> brief sentence
BRIEFS: dict[str, str] = {
    "examples/kallio_pub_crawl_esc/main.mla": "Terminal 'pub crawl' walking-map demo rendered with std::esc.",
    "examples/switch_demo.mla": "Enum-dispatch style switch/match demo using std::strbuf.",
    "examples/thread_mutex_atomic.mla": "Threaded counter demo contrasting mutex and atomic operations.",
    "examples/jack2_lockfree_thread_demo.mla": "Lock-free thread producer/consumer demo modelled after JACK2 audio.",
    "examples/esc_widgets/widgets.mla": "Reusable TUI widget primitives (boxes, bars, labels) built on std::esc.",
    "examples/esc_widgets/tracker_ui_demo.mla": "Terminal music tracker UI demo built on the esc_widgets toolkit.",
    "examples/chat_tui_demo.mla": "Full-screen chat-style TUI demo driven by std::chat.",
    "examples/raii_free_method_demo.mla": "RAII demo showing automatic resource cleanup via a free() method.",
    "examples/for_loop_example.mla": "Basic for-loop iteration example over ranges and lists.",
    "examples/std_vec_demo.mla": "Demonstrates Vec<T> (std::vec) including vec![] literals and helpers.",
    "examples/pointer_access.mla": "Raw pointer access patterns and safety demonstration.",
    "examples/argparser_demo.mla": "Command-line argument parsing demo using std::argparser.",
    "examples/closure_thread.mla": "Spawning a worker thread with a captured closure.",
    "examples/inline_asm_aarch64_demo.mla": "Inline assembly demo targeting AArch64 (ARM64).",
    "examples/pipe_operator_demo.mla": "Pipe (|>) operator demo chaining transformations.",
    "examples/std_span_demo.mla": "Borrowed slice/span example using std::span.",
    "examples/result_usage.mla": "result<T,E> usage showing propagation and unwrap patterns.",
    "examples/borrow_patterns.mla": "Common borrow-checker patterns (immutable/mutable, scopes).",
    "examples/result_match.mla": "Pattern matching on result<T,E> branches.",
    "examples/lambda_fold_advanced.mla": "Advanced fold/reduce demo with lambdas and closures.",
    "examples/fft_example/main.mla": "FFT example driver using std::fft.",
    "examples/main.mla": "Minimal top-level main wiring std::math and a local utils module.",
    "examples/package_manager_workspace_fetch/packages/tarball_cjson_demo/src/main.mla": "Package-manager example: uses a tarball-fetched cJSON native dependency.",
    "examples/package_manager_workspace_fetch/packages/tarball_cjson_demo/src/cjson.mla": "FFI bindings for the tarball-fetched cJSON used by the workspace demo.",
    "examples/package_manager_workspace_fetch/packages/git_cjson_demo/src/main.mla": "Package-manager example: uses a git-fetched cJSON native dependency.",
    "examples/package_manager_workspace_fetch/packages/git_cjson_demo/src/cjson.mla": "FFI bindings for the git-fetched cJSON used by the workspace demo.",
    "examples/c_lib_usage.mla": "Calling into a C library from MLang via FFI.",
    "examples/closures_demo.mla": "Closure capture semantics and higher-order functions.",
    "examples/ffi_cos.mla": "FFI demo calling libm cos() from MLang.",
    "examples/functional_closure_fold_demo.mla": "Functional-style fold over a collection with closures.",
    "examples/std_jsonrpc_runtime_demo.mla": "JSON-RPC runtime dispatcher demo using std::jsonrpc.",
    "examples/map_list.mla": "Working with map<K,V> and list<T> containers.",
    "examples/env_help_demo/main.mla": "std::env::render_help CLI help-text rendering demo.",
    "examples/ternary_example.mla": "Ternary (?:) expression demo.",
    "examples/std_net_demo.mla": "Minimal TCP client/server demo using std::net.",
    "examples/std_rand_demo.mla": "Pseudo-random number generation demo using std::rand.",
    "examples/std_io_traits_demo.mla": "Read/Write/Seek trait-like API demo from std::io.",
    "examples/std_sync_demo.mla": "Synchronization primitives demo (mutex, atomics) from std::sync.",
    "examples/lambda_fold_patterns.mla": "Common fold patterns expressed with lambdas.",
    "examples/thread_multi.mla": "Spawning multiple worker threads and joining them.",
    "examples/package_manager_static_cjson/src/main.mla": "Package-manager demo consuming a statically linked cJSON library.",
    "examples/package_manager_static_cjson/src/cjson.mla": "FFI bindings for the statically linked cJSON dependency.",
    "examples/std_regex_demo.mla": "POSIX regular expression demo using std::regex.",
    "examples/std_json_demo.mla": "Parsing and navigating a JSON document with std::json.",
    "examples/std_process_demo.mla": "Spawning and collecting child processes via std::process.",
    "examples/macos_stdio_driver/main.mla": "macOS stdio-driver TUI example using std::term/std::esc.",
    "examples/std_net_mt_server.mla": "Multithreaded TCP echo server built on std::net and std::thread.",
    "examples/std_protocol_demo.mla": "Length-prefixed framing demo using std::protocol.",
    "examples/enum_backing_valid.mla": "Demonstrates valid explicit enum backing-type declarations.",
    "examples/print_test.mla": "Quick println!/format! output test.",
    "examples/std_sed_demo.mla": "Stream-editor style text substitution demo using std::sed.",
    "examples/ffi_add.mla": "FFI example calling a small C add() function.",
    "examples/std_io_raii_demo.mla": "File RAII demo: files close automatically when scope exits.",
    "examples/tuple_test.mla": "Compile/runtime test of tuple construction and destructuring.",
    "examples/tuple_example.mla": "Tuple basics: construction, indexing, and destructuring.",
    "examples/ga_tsp_esc/main.mla": "Genetic-algorithm Travelling Salesman Problem with an esc-driven TUI.",
    "examples/functional_option_result_demo.mla": "Functional usage of option/result with map/and_then helpers.",
    "examples/std_jsonrpc_stdio_loop_demo.mla": "JSON-RPC request/response loop over stdio using std::jsonrpc.",
    "examples/enum_backing_fail_u8_implicit.mla": "Expected-failure demo: enum value implicitly overflowing u8 backing.",
    "examples/inline_asm_x64_hello_demo.mla": "Inline x86-64 assembly 'hello' demo.",
    "examples/enum_option_match.mla": "Matching on enum option-style variants.",
    "examples/image_truecolor_demo.mla": "Truecolor terminal image rendering demo using std::image and std::esc.",
    "examples/sieve_sundaram.mla": "Sieve of Sundaram prime number generator example.",
    "examples/std_term_demo.mla": "Terminal capability / size detection demo using std::term.",
    "examples/inline_asm_aarch64_hello_demo.mla": "Inline AArch64 assembly 'hello' demo.",
    "examples/bit_sizeof_demo.mla": "Bit-level sizeof() queries on primitive types.",
    "examples/lock_free_queue_post_demo.mla": "Lock-free queue producer/poster demo.",
    "examples/std_net_mt_client.mla": "Multithreaded TCP client hitting the std_net_mt_server demo.",
    "examples/std_compiler_demo.mla": "Using std::compiler to query compiler metadata at runtime.",
    "examples/minimal_vim_demo.mla": "Minimal vim-like modal text editor demo built on std::term.",
    "examples/slice.mla": "Slicing a list into borrowed ranges.",
    "examples/break_continue.mla": "`break`/`continue` inside loops demo.",
    "examples/package_manager_git_cjson/curl.mla": "FFI bindings for libcurl used by the git/cJSON package-manager demo.",
    "examples/package_manager_git_cjson/src/main.mla": "Package-manager demo fetching cJSON from git and using it via FFI.",
    "examples/thread_basic.mla": "Spawning a single worker thread and joining it.",
    "examples/generics_example.mla": "Generics basics: generic functions and structs.",
    "examples/inline_asm_aarch64_data_hello_demo.mla": "AArch64 inline assembly writing a 'hello' message from a data section.",
    "examples/exceptions_try_catch_demo.mla": "`throw`/`try`/`catch` exception-handling demo.",
    "examples/fibonacci.mla": "Recursive and iterative Fibonacci number implementations.",
    "examples/scope_exit_drop_demo.mla": "Scope-exit drop demo showing deterministic cleanup order.",
    "examples/str_types.mla": "str8 vs str16 string types and conversion demo.",
    "examples/c_lib_text_parse_demo.mla": "Using a C text-parsing library from MLang via FFI.",
    "examples/enum_backing_cross_enum.mla": "Demonstrates enum values referencing other enum constants as backing.",
    "examples/stdio_driver_demo.mla": "Low-level stdio driver demo for raw terminal input/output.",
    "examples/bit_packed_struct_demo.mla": "Bit-packed struct fields and layout demo.",
    "examples/enum_print_demo.mla": "Pretty-printing enum variants.",
    "examples/std_math_demo.mla": "std::math numeric helpers demo (add/sub/sin/etc.).",
    "examples/dsp_fft_demo.mla": "dsp::fft forward/inverse FFT demo with sample arrays.",
    "examples/std_gps_demo.mla": "std::gps GPS NMEA parsing / coordinate helpers demo.",
    "examples/notepad_demo.mla": "Minimal terminal notepad editor demo.",
    "examples/std_thread_demo.mla": "std::thread spawn/join API demo.",
    "examples/std_concurrent_demo.mla": "std::concurrent high-level concurrency primitives demo.",
    "examples/sampler_example/jack2_drum_machine.mla": "JACK2 drum-machine sampler example with audio callback plumbing.",
    "examples/inline_asm_x64_data_hello_demo.mla": "x86-64 inline assembly writing a 'hello' message from a data section.",
    "examples/std_io_input_demo.mla": "std::io line-input demo reading from stdin.",
    "examples/ga_tsp/main.mla": "Genetic-algorithm Travelling Salesman Problem solver (console output).",
    "examples/mofe_ga_tsp_esc/main.mla": "Multi-objective GA TSP variant with an esc-driven TUI.",
    "examples/borrowing.mla": "Core borrowing rules demonstration.",
    "examples/update_expression.mla": "Struct update-expression syntax demo.",
    "examples/package_manager_multilanguage_example/src/main.mla": "Multi-language package demo (MLang + C) via the package manager.",
    "examples/platform_region_demo.mla": "std::platform region/locale queries demo.",
    "examples/trait_custom_summary.mla": "Custom trait impl providing a summary method.",
    "examples/utils.mla": "Shared helper functions reused by the top-level main.mla example.",
    "examples/map_iteration.mla": "Iterating over map<K,V> entries.",
    "examples/std_fs_demo.mla": "std::fs filesystem operations demo (read/write/stat).",
    "examples/std_fs_rw.mla": "std::fs read/write round-trip demo.",
    "examples/block_pattern_demo.mla": "Block-pattern expression demo.",
    "examples/pkg_workflow_main.mla": "Tiny main used to verify package-manager build workflow.",
    "examples/std_sed_str16_demo.mla": "std::sed substitution demo on str16 (UTF-16) text.",
    "examples/inline_attrs.mla": "Demonstrates inline attributes (#[inline], etc.).",
    "examples/c_lib_file_io_demo.mla": "Using a C file-I/O library from MLang via FFI.",
    "examples/std_esc_demo.mla": "ANSI escape sequence demo using std::esc.",
    "examples/std_string_demo.mla": "Owned string manipulation helpers demo.",
    "examples/enum_backing_fail_u8_explicit.mla": "Expected-failure demo: explicit u8 enum backing overflow.",
    "examples/generics_test.mla": "Generics edge-case regression test example.",
    "examples/main_args.mla": "Reading command-line arguments via main(argv).",
    "examples/std_io_demo.mla": "std::io println/eprintln/read demo.",
    "examples/protocol_mt/server.mla": "Multithreaded std::protocol framed server demo.",
    "examples/protocol_mt/client.mla": "Multithreaded std::protocol framed client demo.",
    "examples/package_manager_multi_bin/src/inspect.mla": "Package-manager multi-binary demo: inspect subcommand entry point.",
    "examples/package_manager_multi_bin/src/hello.mla": "Package-manager multi-binary demo: hello subcommand entry point.",
    "examples/std_fs_seek.mla": "std::fs seek/tell file-positioning demo.",
    "examples/c_type_mappings.mla": "Mapping MLang primitive types onto C ABI types for FFI.",
    "examples/lambda_fold_demo.mla": "Fold/reduce demo using lambda expressions.",
    "examples/std_time_demo.mla": "std::time clock/duration queries demo.",
    "examples/inline_asm_x64_demo.mla": "Inline x86-64 assembly demo.",
    "examples/std_process_foreground_demo.mla": "std::process foreground child-process demo.",
    "examples/type_alias_demo.mla": "`use type` type-alias declaration demo.",
    "examples/platform_inline_asm_demo.mla": "Platform-dispatched inline assembly selecting arch at compile time.",
    "examples/debug_test.mla": "`#[derive(Debug)]` derive test example.",
    "examples/enum_string_hex_demo.mla": "Printing enum values as strings and hex numbers.",
    "examples/package_manager_multi_bins/src/inspect.mla": "Package-manager multi-binaries demo: inspect entry point.",
    "examples/package_manager_multi_bins/src/hello.mla": "Package-manager multi-binaries demo: hello entry point.",
    "examples/package_manager_multi_bins/src/cjson.mla": "FFI bindings to cJSON used by the multi-binaries package demo.",
    "examples/package_manager_multi_bins/src/zlib.mla": "FFI bindings to zlib used by the multi-binaries package demo.",
    "examples/std_fs_lines.mla": "std::fs line-by-line file reading demo.",
    "examples/std_hash_demo.mla": "std::hash hashing helpers demo.",
    "examples/borrowing_demo.mla": "Additional borrowing semantics demonstration.",
    "examples/std_platform_demo.mla": "std::platform OS/arch detection demo.",
    "examples/uml_ui_generator/src/main.mla": "Main driver for the UML/UI generator example (see @ref uml_ui_generator).",
}


def humanize(path: str) -> str:
    base = os.path.splitext(os.path.basename(path))[0]
    words = base.replace("_", " ").strip()
    return f"Example: {words}."


def build_header(path: str) -> str:
    basename = os.path.basename(path)
    brief = BRIEFS.get(path, humanize(path))
    return (
        f"/// \\file {basename}\n"
        f"/// @ingroup examples\n"
        f"/// \\brief {brief}\n"
        f"///\n"
    )


def already_has_header(text: str) -> bool:
    first = text.splitlines()[:6]
    for line in first:
        stripped = line.strip()
        if stripped.startswith("///") and ("\\file" in stripped or "@file" in stripped):
            return True
    return False


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: add_example_doxygen.py <file.mla> [<file.mla> ...]", file=sys.stderr)
        return 2
    changed = 0
    for path in argv[1:]:
        try:
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
        except OSError as exc:
            print(f"skip {path}: {exc}", file=sys.stderr)
            continue
        if already_has_header(text):
            continue
        header = build_header(path)
        with open(path, "w", encoding="utf-8") as f:
            f.write(header + text)
        changed += 1
        print(f"updated {path}")
    print(f"updated {changed} file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
