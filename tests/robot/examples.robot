*** Settings ***
Library           Process
Library           OperatingSystem
Suite Setup       Initialize Artifact Dir

*** Variables ***
${MLANG}           ${EXECDIR}/build/mlang
@{EXAMPLES}
...    examples/break_continue.mla
...    examples/c_lib_usage.mla
...    examples/c_type_mappings.mla
...    examples/closure_thread.mla
...    examples/closures_demo.mla
...    examples/debug_test.mla
...    examples/enum_option_match.mla
...    examples/ffi_add.mla
...    examples/ffi_cos.mla
...    examples/for_loop_example.mla
...    examples/generics_test.mla
...    examples/inline_attrs.mla
...    examples/main.mla
...    examples/map_iteration.mla
...    examples/map_list.mla
...    examples/mlang_attributes.mla
...    examples/package_manager_git_cjson/src/main.mla
...    examples/std_math_demo.mla
...    examples/std_string_demo.mla
...    examples/std_thread_demo.mla
...    examples/std_fs_demo.mla
...    examples/std_net_demo.mla
...    examples/std_json_demo.mla
...    examples/std_process_demo.mla
...    examples/std_time_demo.mla
...    examples/std_sync_demo.mla
...    examples/pkg_workflow_main.mla
...    examples/pointer_access.mla
...    examples/print_test.mla
...    examples/result_match.mla
...    examples/result_usage.mla
...    examples/slice.mla
...    examples/std_vec_demo.mla
...    examples/str_types.mla
...    examples/ternary_example.mla
...    examples/thread_basic.mla
...    examples/thread_multi.mla
...    examples/thread_mutex_atomic.mla
...    examples/tuple_example.mla
...    examples/tuple_test.mla
...    examples/argparser_demo.mla
...    examples/associated_functions_demo/main.mla
...    examples/generic_trait_bounds_demo/main.mla
...    examples/dyn_trait_demo/main.mla
...    examples/dyn_trait_field_demo/main.mla
...    examples/method_visibility_demo/main.mla
...    examples/module_path_generic_static_demo/main.mla
...    examples/trait_advanced_demo/main.mla
...    examples/type_alias_demo.mla
...    examples/lambda_fold_patterns.mla
...    examples/lambda_fold_advanced.mla
...    examples/testing_mock_example.mla
...    examples/std_fs_lines.mla
...    examples/std_fs_seek.mla
...    examples/std_fs_rw.mla
...    examples/std_net_mt_server.mla
...    examples/std_net_mt_client.mla

*** Test Cases ***
Compile All Examples
    File Should Exist    ${MLANG}
    FOR    ${example}    IN    @{EXAMPLES}
        File Should Exist    ${example}
        ${result}=    Run Process    ${MLANG}    -c    ${example}    stdout=PIPE    stderr=PIPE
        Should Be Equal As Integers    ${result.rc}    0    msg=Failed compiling ${example} (rc=${result.rc})\nSTDOUT:\n${result.stdout}\nSTDERR:\n${result.stderr}
    END

Compile Errors For Conflicting Types
    ${tmp}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/conflicting_types.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct Foo { var x: i32; };
    ...    enum Foo { A };
    Create File    ${tmp}    ${code}
    ${result}=    Run Process    ${MLANG}    -c    ${tmp}    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${result.rc}    0
    Should Contain    ${result.stderr}    type name 'Foo' conflicts with earlier struct defined at line 1

Compile Errors For Reserved Type Keywords
    ${tmp}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/reserved_keyword.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct list { var x: i32; };
    Create File    ${tmp}    ${code}
    ${result}=    Run Process    ${MLANG}    -c    ${tmp}    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${result.rc}    0
    Should Contain    ${result.stderr}    expected identifier, found keyword 'list'

Main Accepts Command Line Arguments
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/main_args.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/main_args_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main(argc: i32, args: list<str8>) -> i32 {
    ...        println!("argc: {}", argc);
    ...        for a in args {
    ...            println!("{}", a);
    ...        }
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed building main args test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    hello    world    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    argc: 3
    Should Contain    ${run.stdout}    hello
    Should Contain    ${run.stdout}    world

Main Return Uses Ternary
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/main_ternary_return.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/main_ternary_return_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let x: i32 = 3;
    ...        return x > 2 ? 7 : 9;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed building ternary return test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    7

Main Defaults To Zero Return
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/main_default_return.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/main_default_return_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        println!("no explicit return");
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed building default return test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0

Compile Error Returns Nonzero
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_error_nonzero.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let x: i32 = ;
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/compile_error_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0    msg=Expected nonzero exit for compile error, got ${build.rc}\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}

Assert Macro Runtime Failure
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/assert_runtime_fail.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/assert_runtime_fail_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        assert!(0);
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed building assert! runtime failure test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    assert! failed

Static Assert Compile Error
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/static_assert_fail.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        static_assert!(1 == 0);
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/static_assert_fail_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0
    Should Contain    ${build.stderr}    static_assert! failed

Static Assert Requires Compile Time Expression
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/static_assert_nonconst_fail.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let x: i32 = 1;
    ...        static_assert!(x > 0);
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/static_assert_nonconst_fail_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0
    Should Contain    ${build.stderr}    static_assert! requires a compile-time boolean expression

Raw Pointer Dereference Requires Unsafe
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/raw_ptr_requires_unsafe.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    extern fn raw_i32_ptr() -> ptr<i32>;
    ...    fn main() -> i32 {
    ...        let p: ptr<i32> = raw_i32_ptr();
    ...        return *p;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/raw_ptr_requires_unsafe_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0
    Should Contain    ${build.stderr}    dereferencing raw pointer requires an unsafe block

Raw Pointer Dereference In Unsafe Compiles
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/raw_ptr_unsafe_ok.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    extern fn raw_i32_ptr() -> ptr<i32>;
    ...    fn main() -> i32 {
    ...        let p: ptr<i32> = raw_i32_ptr();
    ...        unsafe {
    ...            return *p;
    ...        }
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    -c    ${src}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed compile-only unsafe raw pointer test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}

Borrowed Pointer Variable And Move Same Call Fails
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/borrow_ptr_var_move_same_call_fail.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct Post { var content: str8; };
    ...    fn consume_two(a: ptr<Post>, b: Post) -> i32 { return 0; }
    ...    fn main() -> i32 {
    ...        let p: Post = Post { content: "x" };
    ...        let q: ptr<Post> = &p;
    ...        return consume_two(q, p);
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/borrow_ptr_var_move_same_call_fail_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0
    Should Contain    ${build.stderr}    cannot move 'p' while borrowed in call

Borrowed Pointer Variable Overlap In Call Fails
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/borrow_ptr_var_overlap_call_fail.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct Post { var content: str8; };
    ...    fn inspect_two(a: ptr<Post>, b: ptr<Post>) -> i32 { return 0; }
    ...    fn main() -> i32 {
    ...        let p: Post = Post { content: "x" };
    ...        let q: ptr<Post> = &p;
    ...        return inspect_two(q, &p);
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/borrow_ptr_var_overlap_call_fail_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0
    Should Contain    ${build.stderr}    cannot borrow 'p'

Mutable Borrow Call Arg Rejected While Shared Borrow Active
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/borrow_mut_call_shared_fail.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct Post { var content: str8; };
    ...    fn inspect_one(a: ptr<Post>) -> i32 { return 0; }
    ...    fn main() -> i32 {
    ...        var p: Post = Post { content: "x" };
    ...        let q: ptr<Post> = &p;
    ...        return inspect_one(&mut p);
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${ARTIFACT DIR}/borrow_mut_call_shared_fail_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0
    Should Contain    ${build.stderr}    cannot borrow 'p' as mutable because it is already borrowed

result Methods And Unwrap Warns
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/result_unwrap_warn.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/result_unwrap_warn_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let r: result<i32, str8> = Ok<i32, str8>(42);
    ...        if r.is_ok(): {
    ...            println!("{}", r.unwrap());
    ...        } else: {
    ...            println!("err");
    ...        }
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed building result unwrap test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    Should Contain    ${build.stderr}    result.unwrap() may panic
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    42

Mlang Test Runner
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/test_runner.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_ok() -> i32 {
    ...        return 0;
    ...    }
    ...    \#[test]
    ...    fn test_fail() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${MLANG}    test    ${src}
    ...    stdout=PIPE    stderr=PIPE    cwd=${ARTIFACT DIR}    env:PATH=${ARTIFACT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    1    msg=Expected 1 failing test, got ${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Mlang Test Sample Directory
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/sample_suite_dir
    Create Directory    ${suite_dir}
    ${src}=    Catenate    SEPARATOR=    ${suite_dir}/sample_suite_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn sample_directory_test_ok() -> i32 {
    ...        return 0;
    ...    }
    ...    \#[test]
    ...    fn sample_directory_math_ok() -> i32 {
    ...        let v: i32 = 2 + 2;
    ...        if v == 4: {
    ...            return 0;
    ...        }
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${MLANG}    test    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE    cwd=${ARTIFACT DIR}    env:PATH=${ARTIFACT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0    msg=Expected sample tests to pass, got ${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Mlang Bench Runner
    [Documentation]    Run stdlib benchmark suite with bench mode and verify benchmark output.
    ${run}=    Run Process    ${MLANG}    bench    ${EXECDIR}/tests/bench_stdlib.mla    --bench-iters    200    --bench-warmup    50
    ...    stdout=PIPE    stderr=PIPE    cwd=${ARTIFACT DIR}    env:PATH=${ARTIFACT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=bench_stdlib failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [BENCH]
    Should Contain    ${run.stdout}    bench_vec_push_pop
    Should Contain    ${run.stdout}    bench_quickmap_hash_set_get
    Should Contain    ${run.stdout}    bench_quickmap_vec_set_get
    Should Contain    ${run.stdout}    bench_exception_try_no_throw
    Should Contain    ${run.stdout}    bench_exception_throw_catch
    Should Contain    ${run.stdout}    bench_exception_throw_catch_unwind

Type Inference Regression
    ${run}=    Run Process    ${MLANG}    test    ${EXECDIR}/tests/type_inference_tests.mla
    ...    stdout=PIPE    stderr=PIPE    cwd=${ARTIFACT DIR}    env:PATH=${ARTIFACT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0    msg=Type inference regression failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Closures Demo Runs Correctly
    [Documentation]    Build and run examples/closures_demo.mla, verify key
    ...                output lines for compound assignment, inline capturing
    ...                closures, and thread::spawn with a closure literal.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/closures_demo_bin
    ${build}=    Run Process    ${MLANG}    examples/closures_demo.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building closures_demo.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=closures_demo exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    # Inline capturing closure -- counter increments correctly
    Should Contain    ${run.stdout}    final count: 3
    # Compound assignment operators
    Should Contain    ${run.stdout}    2 + 3  = 5
    Should Contain    ${run.stdout}    5 * 4  = 20
    Should Contain    ${run.stdout}    20 - 8 = 12
    Should Contain    ${run.stdout}    12 / 3 = 4
    # Accumulator closure called in a loop
    Should Contain    ${run.stdout}    sum of 5 calls to add (+=10 each): 50
    # Two independent closures
    Should Contain    ${run.stdout}    x (0 + 3*3) = 9
    Should Contain    ${run.stdout}    y (1 * 2^3) = 8
    # Logic inside closure (evens/odds)
    Should Contain    ${run.stdout}    evens=4 odds=4
    # thread::spawn with closure -- spawned output appears (order not checked)
    Should Contain    ${run.stdout}    [thread 1] hello from spawned closure!
    Should Contain    ${run.stdout}    [thread 2] sum(1..3) = 6
    # Rust-style spawn pattern
    Should Contain    ${run.stdout}    hi from the main thread!
    Should Contain    ${run.stdout}    hi number 1 from the spawned thread!
    Should Contain    ${run.stdout}    hi number 3 from the spawned thread!

Closure Tests Pass
    [Documentation]    Run tests/closure_tests.mla through the mlang test runner.
    ...                Covers compound assignment and inline capturing closures.
    ${run}=    Run Process    ${MLANG}    test    ${EXECDIR}/tests/closure_tests.mla
    ...    stdout=PIPE    stderr=PIPE    cwd=${ARTIFACT DIR}    env:PATH=${ARTIFACT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=closure_tests failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pass=17

Inline Attrs Demo Runs Correctly
    [Documentation]    Build and run examples/inline_attrs.mla; verify that
    ...                #[inline], #[inline(always)], and #[inline(never)]
    ...                compile and produce correct output.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/inline_attrs_bin
    ${build}=    Run Process    ${MLANG}    examples/inline_attrs.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building inline_attrs.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=inline_attrs exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    add(12, 8): 20
    Should Contain    ${run.stdout}    square(7): 49
    Should Contain    ${run.stdout}    clamp(15, 0..10): 10

MLang Frontend Wrapper Compiles And Forwards
    [Documentation]    Build tools/mlang-frontend-mla/main.mla and verify it forwards args to backend mlang.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building mlang frontend wrapper (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=Frontend wrapper failed forwarding --version (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend Missing Backend Value Errors
    [Documentation]    Verify frontend reports parse error and usage when --backend has no value.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_parse_err_backend
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    2
    Should Contain    ${run.stderr}    missing value after --backend
    Should Contain    ${run.stdout}    Usage:

MLang Frontend MissingBackendBeforeHelpErrors
    [Documentation]    Verify malformed --backend takes precedence over help when backend value is missing.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_parse_err_backend_help
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    2
    Should Contain    ${run.stderr}    missing value after --backend
    Should Contain    ${run.stdout}    Usage:

MLang Frontend MissingBackendBeforeVersionErrors
    [Documentation]    Verify malformed --backend takes precedence over version when backend value is missing.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_parse_err_backend_version
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    2
    Should Contain    ${run.stderr}    missing value after --backend
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RepeatedBackendMissingValueErrors
    [Documentation]    Verify a repeated top-level --backend without value is treated as a parse error.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_parse_err_backend_repeat
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --backend
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    2
    Should Contain    ${run.stderr}    missing value after --backend
    Should Contain    ${run.stdout}    Usage:

MLang Frontend NoPassthrough PrintsUsage
    [Documentation]    Verify wrapper with no passthrough args prints usage and exits nonzero.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_no_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    1
    Should Contain    ${run.stderr}    Error: No input file specified
    Should Contain    ${run.stdout}    Usage:

MLang Frontend BackendOnly PrintsUsage
    [Documentation]    Verify wrapper with only --backend still requires passthrough args and prints usage.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_backend_only
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    1
    Should Contain    ${run.stderr}    Error: No input file specified
    Should Contain    ${run.stdout}    Usage:

MLang Frontend CompileFlagsOnly PrintsNoInputError
    [Documentation]    Verify C++ parity: compile mode with only flags errors with no-input message and does not invoke backend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_flags_only
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_flags_only_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_flags_only_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "invoked:$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    -O2
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    1
    Should Contain    ${run.stderr}    Error: No input file specified
    Should Contain    ${run.stdout}    Usage:
    ${exists}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${exists}    ${False}

MLang Frontend CompileOnly LinkOrOutput WithValue StillNoInputError
    [Documentation]    Verify C++ parity: -o/-L/-l with values but no input file still reports no-input error.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_link_or_output_no_input
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0

    ${run_o}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    -o    out.bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run_o.rc}    1
    Should Contain    ${run_o.stderr}    Error: No input file specified
    Should Contain    ${run_o.stdout}    Usage:

    ${run_L}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    -L    /tmp/somelib
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run_L.rc}    1
    Should Contain    ${run_L.stderr}    Error: No input file specified
    Should Contain    ${run_L.stdout}    Usage:

    ${run_l}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    -l    somelib
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run_l.rc}    1
    Should Contain    ${run_l.stderr}    Error: No input file specified
    Should Contain    ${run_l.stdout}    Usage:

MLang Frontend CompileOnly TestsFlag WithoutInput IsAccepted
    [Documentation]    Verify C++ parity: compile-stream --tests without explicit input is accepted (defaults handled by backend).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_tests_no_input
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_no_input_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_no_input_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while

MLang Frontend CompileOnly TestsFlag InvalidBenchValue FailsEarly
    [Documentation]    Verify C++ parity: compile-stream --tests without explicit input still validates bench option values early.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_tests_invalid_bench
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_invalid_bench_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_invalid_bench_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    --bench-iters    nope
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-iters
    ${exists}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${exists}    ${False}

MLang Frontend CompileOnly TestsFlag NoRun IsAccepted
    [Documentation]    Verify C++ parity: bare compile-stream --tests accepts and forwards --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_tests_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_norun_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    --no-run

MLang Frontend CompileOnly TestsFlag UnknownOption Fails
    [Documentation]    Verify C++ parity: bare compile-stream --tests rejects unknown options with usage.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_tests_unknown
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    --tests    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

MLang Frontend CompileOnly TestsFlag OutputOption IsAcceptedAndIgnoredInDirMode
    [Documentation]    Verify C++ parity: bare compile-stream --tests accepts -o <file> but does not forward it per-suite in directory mode.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_tests_output
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_output_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_only_tests_output_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    -o    mlang_test_out
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Not Contain    ${log_text}    -o mlang_test_out

MLang Frontend CompileOnly TestsFlag MissingOutputValue Fails
    [Documentation]    Verify C++ parity: bare compile-stream --tests with missing -o value reports unknown option and usage.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_only_tests_missing_output
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    --tests    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: -o
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Last Backend option Wins
    [Documentation]    Verify wrapper parsing uses the last --backend value before passthrough args.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_last_backend
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend1}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_last_backend_1.sh
    ${fake_backend2}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_last_backend_2.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_last_backend.log
    ${script1}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "backend1:$@" >> "${fake_log}"
    ...    exit 7
    ${script2}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "backend2:$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend1}    ${script1}
    Create File    ${fake_backend2}    ${script2}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc
    ...    chmod +x "${fake_backend1}" "${fake_backend2}"
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend1}
    ...    --backend    ${fake_backend2}
    ...    test    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    backend2:--version
    Should Not Contain    ${log_text}    backend1:

MLang Frontend BackendOption In Passthrough Is Not Parsed
    [Documentation]    Verify --backend after passthrough start is treated as regular passthrough arg (not frontend option).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_backend_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_backend_passthrough.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_backend_passthrough.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    dummy_input.mla    --backend    someone_else
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    dummy_input.mla --backend someone_else

MLang Frontend BackendWithoutValueInPassthroughIsForwarded
    [Documentation]    Verify --backend after passthrough start is forwarded even when it has no following value.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_backend_passthrough_novalue
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_backend_passthrough_novalue.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_backend_passthrough_novalue.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    dummy_input.mla    --backend
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    dummy_input.mla --backend

MLang Frontend Normalizes Signaled Backend Exit To One
    [Documentation]    Verify frontend maps signaled backend termination to exit code 1.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_backend_signal_exit
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${signal_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_signal_backend.sh
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    kill -ABRT $$
    Create File    ${signal_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${signal_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${signal_backend}    dummy_input.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    1

MLang Frontend Preserves Normal Backend Exit Code
    [Documentation]    Verify frontend forwards normal exited backend return code without normalization.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_backend_exit_code
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${exit_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_exit_backend.sh
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    exit 7
    Create File    ${exit_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${exit_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${exit_backend}    dummy_input.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    7

MLang Frontend Missing Backend Executable Returns 127
    [Documentation]    Verify frontend returns 127 when backend executable path does not exist.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_backend_spawn_fail
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /definitely/not/a/real/backend    dummy_input.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    127

MLang Frontend TopLevelVersionShortCircuitsPassthrough
    [Documentation]    Verify top-level --version before passthrough short-circuits and does not invoke backend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_version_short
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_version_short_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_version_short_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    --version    test
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang-frontend-mla
    ${invoked}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${invoked}    ${False}

MLang Frontend TopLevelHelpShortCircuitsPassthrough
    [Documentation]    Verify top-level --help before passthrough short-circuits and does not invoke backend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_help_short
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_help_short_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_help_short_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    --help    test
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    ${invoked}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${invoked}    ${False}

MLang Frontend TopLevelShortHelpShortCircuitsPassthrough
    [Documentation]    Verify top-level -h before passthrough short-circuits and does not invoke backend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_shorthelp_short
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_shorthelp_short_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_shorthelp_short_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    -h    test
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    ${invoked}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${invoked}    ${False}

MLang Frontend TopLevelHelp Before Version Uses Help
    [Documentation]    Verify top-level flag order parity: --help before --version short-circuits as help.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_help_before_version
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --help    --version    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    Should Not Contain    ${run.stdout}    mlang-frontend-mla 

MLang Frontend TopLevelVersion Before Help Uses Version
    [Documentation]    Verify top-level flag order parity: --version before --help short-circuits as version.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_version_before_help
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --version    --help    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang-frontend-mla
    Should Not Contain    ${run.stdout}    Usage:

MLang Frontend TopLevelShortHelp Before Version Uses Help
    [Documentation]    Verify top-level flag order parity: -h before --version short-circuits as help.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_shorthelp_before_version
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    -h    --version    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    Should Not Contain    ${run.stdout}    mlang-frontend-mla 

MLang Frontend TopLevelVersion Before ShortHelp Uses Version
    [Documentation]    Verify top-level flag order parity: --version before -h short-circuits as version.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_version_before_shorthelp
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --version    -h    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang-frontend-mla
    Should Not Contain    ${run.stdout}    Usage:

MLang Frontend TopLevelVersion Before MissingBackendValue Succeeds
    [Documentation]    Verify top-level --version short-circuits even if malformed --backend appears later.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_version_before_missing_backend
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --version    --backend
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang-frontend-mla
    Should Not Contain    ${run.stderr}    missing value after --backend

MLang Frontend TopLevelHelp Before MissingBackendValue Succeeds
    [Documentation]    Verify top-level --help short-circuits even if malformed --backend appears later.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_help_before_missing_backend
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --help    --backend
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    Should Not Contain    ${run.stderr}    missing value after --backend

MLang Frontend TopLevelHelp Before UnknownToken ShortCircuits
    [Documentation]    Verify top-level --help short-circuits frontend parsing even with trailing unknown passthrough token.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_help_before_unknown_token
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_help_before_unknown_token_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_help_before_unknown_token_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --help    --definitely-unknown-arg
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    ${invoked}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${invoked}    ${False}

MLang Frontend TopLevelVersion Before UnknownToken ShortCircuits
    [Documentation]    Verify top-level --version short-circuits frontend parsing even with trailing unknown passthrough token.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_version_before_unknown_token
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_version_before_unknown_token_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_version_before_unknown_token_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --version    --definitely-unknown-arg
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang-frontend-mla
    ${invoked}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${invoked}    ${False}

MLang Frontend TopLevelShortHelp Before UnknownToken ShortCircuits
    [Documentation]    Verify top-level -h short-circuits frontend parsing even with trailing unknown passthrough token.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_toplevel_shorthelp_before_unknown_token
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_shorthelp_before_unknown_token_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_toplevel_shorthelp_before_unknown_token_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    -h    --definitely-unknown-arg
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:
    ${invoked}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${invoked}    ${False}

MLang Frontend UnknownToken Before TopLevelHelp Is Forwarded
    [Documentation]    Verify once passthrough starts, trailing --help is forwarded to backend and not consumed by frontend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_unknown_before_toplevel_help
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_unknown_before_toplevel_help_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_unknown_before_toplevel_help_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --definitely-unknown-arg    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --definitely-unknown-arg --help
    Should Not Contain    ${run.stdout}    Usage:

MLang Frontend UnknownToken Before TopLevelShortHelp Is Forwarded
    [Documentation]    Verify once passthrough starts, trailing -h is forwarded to backend and not consumed by frontend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_unknown_before_toplevel_shorthelp
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_unknown_before_toplevel_shorthelp_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_unknown_before_toplevel_shorthelp_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --definitely-unknown-arg    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --definitely-unknown-arg -h
    Should Not Contain    ${run.stdout}    Usage:

MLang Frontend UnknownToken Before TopLevelVersion Is Forwarded
    [Documentation]    Verify once passthrough starts, trailing --version is forwarded to backend and not consumed by frontend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_unknown_before_toplevel_version
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_unknown_before_toplevel_version_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_unknown_before_toplevel_version_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --definitely-unknown-arg    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --definitely-unknown-arg --version
    Should Not Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend PostPassthroughVersionIsForwarded
    [Documentation]    Verify --version after passthrough start is forwarded to backend, not treated as top-level frontend flag.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_post_passthrough_version
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_post_passthrough_version_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_post_passthrough_version_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    dummy_input.mla    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    dummy_input.mla --version

MLang Frontend PostPassthroughHelpIsForwarded
    [Documentation]    Verify --help after passthrough start is forwarded to backend, not treated as top-level frontend flag.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_post_passthrough_help
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_post_passthrough_help_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_post_passthrough_help_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    dummy_input.mla    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    dummy_input.mla --help

MLang Frontend PostPassthroughShortHelpIsForwarded
    [Documentation]    Verify -h after passthrough start is forwarded to backend, not treated as top-level frontend flag.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_post_passthrough_shorthelp
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_post_passthrough_shorthelp_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_post_passthrough_shorthelp_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}
    ...    --backend    ${fake_backend}
    ...    dummy_input.mla    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    dummy_input.mla -h

MLang Frontend Test Version Uses Backend Semantics
    [Documentation]    Verify `test --version` is passed through and reports backend version semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_version_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    test    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang
    Should Not Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend Test Help Uses Backend Semantics
    [Documentation]    Verify `test --help` is passed through and uses backend help text.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_help_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    test    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    mlang-frontend-mla
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test Unknown Before Help Fails
    [Documentation]    Verify argument order parity: unknown option before --help in test mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Test Unknown Before ShortHelp Fails
    [Documentation]    Verify argument order parity: unknown option before -h in test mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_unknown_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --definitely-unknown-flag    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend NoRun Before TrailingTests Fails
    [Documentation]    Verify left-to-right C++ parity: `--no-run` before trailing `--tests` is an unknown option.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_norun_before_trailing_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_norun_before_trailing_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --no-run    --tests    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --no-run

MLang Frontend Test NoRunBeforeTests ThenRuns
    [Documentation]    Verify C++ parity: in test mode, later --tests overrides earlier --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_norun_then_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_norun_then_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_fail_norun_then_tests() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --no-run    --tests    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    [FAIL]
    Should Contain    ${run.stdout}    [SUMMARY]

MLang Frontend RunTests NoRunBeforeTests ThenRuns
    [Documentation]    Verify C++ parity: in run tests mode, later --tests overrides earlier --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_norun_then_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_norun_then_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_fail_runtests_norun_then_tests() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --no-run    --tests    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    [FAIL]
    Should Contain    ${run.stdout}    [SUMMARY]

MLang Frontend Test FinalNoRunSkips
    [Documentation]    Verify C++ parity: repeated --tests/--no-run in test mode follows last-flag-wins semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_final_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_final_norun.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_fail_if_run_final_norun() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --tests    --no-run    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    [FAIL]
    Should Not Contain    ${run.stdout}    [SUMMARY]

MLang Frontend RunTests FinalNoRunSkips
    [Documentation]    Verify C++ parity: repeated --tests/--no-run in run tests mode follows last-flag-wins semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_final_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_final_norun.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_fail_if_run_runtests_final_norun() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --tests    --no-run    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    [FAIL]
    Should Not Contain    ${run.stdout}    [SUMMARY]

MLang Frontend DirectTests NoRunBeforeTests ThenRuns
    [Documentation]    Verify direct --tests mode matches C++ parity: later --tests overrides earlier --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_norun_then_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_norun_then_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_should_fail_if_run() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --no-run    --tests    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    [FAIL]
    Should Contain    ${run.stdout}    [SUMMARY]

MLang Frontend DirectTests FinalNoRunSkips
    [Documentation]    Verify direct --tests mode matches C++ parity: repeated --tests/--no-run follows last-flag-wins semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_final_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_final_norun.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_should_fail_if_run_final_norun() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --tests    --no-run    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    [FAIL]
    Should Not Contain    ${run.stdout}    [SUMMARY]

MLang Frontend Test Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in test mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test Help Before MissingValueOption Succeeds
    [Documentation]    Verify `test --help -o` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_help_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --help    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test ShortHelp Before Unknown Succeeds
    [Documentation]    Verify argument order parity: -h before unknown option in test mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test ShortHelp Before MissingValueOption Succeeds
    [Documentation]    Verify `test -h -o` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_shorthelp_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    -h    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in test mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Test Version Before MissingValueOption Succeeds
    [Documentation]    Verify `test --version -L` short-circuits to backend version and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_version_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --version    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Test Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in test mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_unknown_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --definitely-unknown-flag    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend DirectTests Help Uses Backend Semantics
    [Documentation]    Verify `--tests --help` is passed through and uses backend help text.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_help_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --tests    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    mlang-frontend-mla
    Should Contain    ${run.stdout}    Usage:

MLang Frontend DirectTests Version Uses Backend Semantics
    [Documentation]    Verify `--tests --version` is passed through and reports backend version semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_version_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --tests    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang
    Should Not Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend DirectTests Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in direct --tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend DirectTests Help Before MissingValueOption Succeeds
    [Documentation]    Verify `--tests --help -o` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_help_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --help    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend DirectTests Unknown Before Help Fails
    [Documentation]    Verify argument order parity: unknown option before --help in direct --tests mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend DirectTests ShortHelp Before Unknown Succeeds
    [Documentation]    Verify argument order parity: -h before unknown option in direct --tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend DirectTests ShortHelp Before MissingValueOption Succeeds
    [Documentation]    Verify `--tests -h -L` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_shorthelp_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    -h    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend DirectTests Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in direct --tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend DirectTests Version Before MissingValueOption Succeeds
    [Documentation]    Verify `--tests --version -l` short-circuits to backend version and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_version_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --version    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend DirectTests Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in direct --tests mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_unknown_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    --tests    --definitely-unknown-flag    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend RunTests Help Uses Backend Semantics
    [Documentation]    Verify `run tests --help` is passed through and uses backend help text.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_help_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    run    tests    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    mlang-frontend-mla
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests Unknown Before Help Fails
    [Documentation]    Verify argument order parity: unknown option before --help should still fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend RunTests Unknown Before ShortHelp Fails
    [Documentation]    Verify argument order parity: unknown option before -h in run tests mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_unknown_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --definitely-unknown-flag    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend RunTests Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in run tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests Help Before MissingValueOption Succeeds
    [Documentation]    Verify `run tests --help -o` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_help_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --help    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests ShortHelp Before Unknown Succeeds
    [Documentation]    Verify argument order parity: -h before unknown option in run tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests ShortHelp Before MissingValueOption Succeeds
    [Documentation]    Verify `run tests -h -L` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_shorthelp_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    -h    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in run tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend RunTests Version Before MissingValueOption Succeeds
    [Documentation]    Verify `run tests --version -L` short-circuits to backend version and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_version_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --version    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend RunTests Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in run tests mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_unknown_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --definitely-unknown-flag    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Bench Help Uses Backend Semantics
    [Documentation]    Verify `bench --help` is passed through and uses backend help text.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_help_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    bench    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    mlang-frontend-mla
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench Version Uses Backend Semantics
    [Documentation]    Verify `bench --version` is passed through and reports backend version semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_version_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    bench    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang
    Should Not Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend Bench ValuePosition Version Is Invalid
    [Documentation]    Verify `--bench-iters --version` treats --version as invalid value, not as version command.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_valuepos_version
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --bench-iters    --version    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-iters

MLang Frontend Bench WarmupValuePosition Help Is Invalid
    [Documentation]    Verify `--bench-warmup --help` treats --help as invalid value, not as help command.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_warmup_valuepos_help
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --bench-warmup    --help    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-warmup

MLang Frontend RunTests Version Uses Backend Semantics
    [Documentation]    Verify `run tests --version` is passed through and reports backend version semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_version_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    run    tests    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang
    Should Not Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend Bench ShortHelp Uses Backend Semantics
    [Documentation]    Verify `bench -h` is passed through and uses backend help text.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_shorthelp_passthrough
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    bench    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Not Contain    ${run.stdout}    mlang-frontend-mla
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench Unknown Before Help Fails
    [Documentation]    Verify argument order parity: unknown option before --help in bench mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Bench Unknown Before ShortHelp Fails
    [Documentation]    Verify argument order parity: unknown option before -h in bench mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_unknown_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --definitely-unknown-flag    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Bench Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in bench mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench ShortHelp Before Unknown Succeeds
    [Documentation]    Verify argument order parity: -h before unknown option in bench mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench ShortHelp Before MissingValueOption Succeeds
    [Documentation]    Verify `bench -h -l` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_shorthelp_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    -h    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in bench mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Bench Help Before MissingValueOption Succeeds
    [Documentation]    Verify `bench --help -l` short-circuits to backend help and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_help_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --help    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench Version Before MissingValueOption Succeeds
    [Documentation]    Verify `bench --version -o` short-circuits to backend version and ignores trailing missing-value options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_version_before_missing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --version    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Bench Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in bench mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_unknown_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --definitely-unknown-flag    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Wrapper Test Dispatch Works
    [Documentation]    Build frontend wrapper and verify `test` + `run tests` dispatch on a temporary suite directory.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dispatch
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building frontend wrapper (dispatch) (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dispatch_suite
    Create Directory    ${suite_dir}
    ${t1}=    Catenate    SEPARATOR=    ${suite_dir}/test_one_tests.mla
    ${t2}=    Catenate    SEPARATOR=    ${suite_dir}/test_two_tests.mla
    ${code1}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_ok() -> i32 {
    ...        return 0;
    ...    }
    ${code2}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${t1}    ${code1}
    Create File    ${t2}    ${code2}
    ${run1}=    Run Process    ${frontend}    --backend    ${MLANG}    test    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run1.rc}    0
    ...    msg=frontend test dispatch failed (rc=${run1.rc})\nSTDOUT:\n${run1.stdout}\nSTDERR:\n${run1.stderr}
    ${run2}=    Run Process    ${frontend}    --backend    ${MLANG}    run    tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run2.rc}    0
    ...    msg=frontend run tests dispatch failed (rc=${run2.rc})\nSTDOUT:\n${run2.stdout}\nSTDERR:\n${run2.stderr}

MLang Frontend RunTests AbsoluteDirectory Works
    [Documentation]    Verify `run tests <absolute_dir>` executes suite discovery for external directories (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_absdir
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${TEMPDIR}/frontend_runtests_absdir_suite
    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${t1}=    Catenate    SEPARATOR=    ${suite_dir}/test_abs_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_abs_ok() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${t1}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    run    tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend run tests absdir failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [SUITE] test_abs_tests.mla
    Should Contain    ${run.stdout}    [SUITE PASS] test_abs_tests.mla

MLang Frontend RunTests NoRun AbsoluteDirectory Works
    [Documentation]    Verify `run tests --no-run <absolute_dir>` keeps no-run semantics while discovering external suites.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_norun_absdir
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${TEMPDIR}/frontend_runtests_norun_absdir_suite
    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${t1}=    Catenate    SEPARATOR=    ${suite_dir}/test_norun_abs_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_norun_abs_ok() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${t1}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    run    tests    --no-run    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend run tests --no-run absdir failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [SUITE] test_norun_abs_tests.mla
    Should Contain    ${run.stdout}    [SUITE PASS] test_norun_abs_tests.mla
    Should Not Contain    ${run.stdout}    [PASS]

MLang Frontend Does Not Intercept NonRunTests Prefix
    [Documentation]    Verify only exact `run tests` is intercepted; other `run ...` forms are forwarded unchanged.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_non_runtests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_non_runtests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_non_runtests_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    Run Keyword And Ignore Error    Remove File    ${fake_log}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    testz    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    run testz --help

MLang Frontend Test Defaults To Tests Directory
    [Documentation]    Verify `test` without explicit path defaults to `tests` directory (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_default_path
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_default_path_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_default_path_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    test
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/

MLang Frontend RunTests Defaults To Tests Directory
    [Documentation]    Verify `run tests` without explicit path defaults to `tests` directory (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_default_path
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_default_path_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_default_path_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    run    tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/

MLang Frontend DirectTests Defaults To Tests Directory
    [Documentation]    Verify direct `--tests` without explicit path defaults to `tests` directory (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_default_path
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_default_path_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_default_path_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/

MLang Frontend Bench Defaults To Tests Directory
    [Documentation]    Verify `bench` without explicit path defaults to `tests` directory (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_default_path
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_path_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_path_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench tests/bench_

MLang Frontend Test DefaultPath Forwards NoRun
    [Documentation]    Verify `test --no-run` without explicit path defaults to tests/ and forwards --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_default_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_default_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_default_norun_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    test    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/
    Should Contain    ${log_text}    --no-run

MLang Frontend Test DefaultPath Forwards LinkerFlags
    [Documentation]    Verify `test -L<dir> -l<name>` without explicit path defaults to tests/ and forwards linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_default_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_default_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_default_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    -L/tmp/mlang_default_test_lib    -ldefaulttestdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/
    Should Contain    ${log_text}    -L/tmp/mlang_default_test_lib
    Should Contain    ${log_text}    -ldefaulttestdep

MLang Frontend RunTests DefaultPath Forwards NoRun
    [Documentation]    Verify `run tests --no-run` without explicit path defaults to tests/ and forwards --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_default_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_default_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_default_norun_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    run    tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/
    Should Contain    ${log_text}    --no-run

MLang Frontend DirectTests DefaultPath Forwards NoRun
    [Documentation]    Verify direct `--tests --no-run` without explicit path defaults to tests/ and forwards --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_default_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_default_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_default_norun_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/
    Should Contain    ${log_text}    --no-run

MLang Frontend RunTests DefaultPath Forwards LinkerFlags
    [Documentation]    Verify `run tests -L<dir> -l<name>` without explicit path defaults to tests/ and forwards linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_default_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_default_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_default_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    -L/tmp/mlang_default_runtests_lib    -ldefaultruntestsdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/
    Should Contain    ${log_text}    -L/tmp/mlang_default_runtests_lib
    Should Contain    ${log_text}    -ldefaultruntestsdep

MLang Frontend DirectTests DefaultPath Forwards LinkerFlags
    [Documentation]    Verify direct `--tests -L<dir> -l<name>` without explicit path defaults to tests/ and forwards linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_default_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_default_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_default_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    -L/tmp/mlang_default_directtests_lib    -ldefaultdirecttestsdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests tests/
    Should Contain    ${log_text}    -L/tmp/mlang_default_directtests_lib
    Should Contain    ${log_text}    -ldefaultdirecttestsdep

MLang Frontend DirectTests Directory Forwards NoRun
    [Documentation]    Verify direct `--tests <dir> --no-run` forwards --no-run to per-suite backend calls (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_norun_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_norun_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_norun_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${suite_dir}    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_norun_tests.mla
    Should Contain    ${log_text}    --no-run

MLang Frontend DirectTests Directory NoRunThenTests Does Not Forward NoRun
    [Documentation]    Verify direct `--tests <dir>` keeps last-flag-wins semantics: `--tests --no-run --tests <dir>` does not forward --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_norun_then_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_norun_then_tests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_norun_then_tests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_norun_then_tests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_norun_then_tests_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_norun_then_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    --no-run    --tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_norun_then_tests.mla
    Should Not Contain    ${log_text}    --no-run

MLang Frontend DirectTests Directory TestsThenNoRun Forwards NoRun
    [Documentation]    Verify direct `--tests <dir>` keeps last-flag-wins semantics: `--tests --tests --no-run <dir>` forwards --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_tests_then_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_tests_then_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_tests_then_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_tests_then_norun_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_tests_then_norun_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_tests_then_norun.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    --tests    --no-run    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_tests_then_norun.mla
    Should Contain    ${log_text}    --no-run

MLang Frontend DirectTests Directory Does Not Forward NoTests
    [Documentation]    Verify direct `--tests <dir> --no-tests` does not forward --no-tests into per-suite calls (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_notests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_notests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_notests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_notests_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_notests_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${suite_dir}    --no-tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_notests_tests.mla
    Should Not Contain    ${log_text}    --no-tests

MLang Frontend DirectTests Directory Ignores CompileFlags
    [Documentation]    Verify direct `--tests <dir>` strips compile-only flags from per-suite backend calls (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_compileflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_compileflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_compileflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_compileflags_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_compileflags_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_compileflags_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${suite_dir}    -c    -S    -emit-llvm    -emit-bc    -O3    -v    --debug
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_compileflags_tests.mla
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-c( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-S( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-emit-llvm( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-emit-bc( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-O3( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-v( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )--debug( |$).*

MLang Frontend DirectTests Directory Ignores OutputFlag
    [Documentation]    Verify direct `--tests <dir> -o <file>` is ignored in directory mode (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_ignore_o
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_ignore_o_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_ignore_o_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_ignore_o_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_ignore_o_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_ignore_o_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${suite_dir}    -o    ${ARTIFACT DIR}/ignored_directtests_dir_bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_ignore_o_tests.mla
    Should Not Contain    ${log_text}    -o
    Should Not Contain    ${log_text}    ignored_directtests_dir_bin

MLang Frontend DirectTests Directory Forwards Split LinkerFlags
    [Documentation]    Verify direct `--tests <dir> -L <dir> -l <name>` forwards linker flags per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_split_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_split_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_split_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_split_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_split_link_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_split_link_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${suite_dir}    -L    /tmp/mlang_directtests_dir_link    -l    directtestsdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_split_link_tests.mla
    Should Contain    ${log_text}    -L /tmp/mlang_directtests_dir_link
    Should Contain    ${log_text}    -l directtestsdep

MLang Frontend DirectTests Directory Forwards Compact LinkerFlags
    [Documentation]    Verify direct `--tests <dir> -Lfoo -lbar -Wl,...` forwards compact linker flags per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_compact_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_compact_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_compact_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_dir_compact_link_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_directtests_dir_compact_link_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${suite_dir}    -L/tmp/mlang_directtests_dir_compact    -ldirecttestscompactdep    -Wl,-rpath,/tmp/mlang_directtests_dir_compact
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_dir_compact_link_tests.mla
    Should Contain    ${log_text}    -L/tmp/mlang_directtests_dir_compact
    Should Contain    ${log_text}    -ldirecttestscompactdep
    Should Contain    ${log_text}    -Wl,-rpath,/tmp/mlang_directtests_dir_compact

MLang Frontend DirectTests Directory Uses Sorted Suite Order
    [Documentation]    Verify direct `--tests <dir>` executes suites in deterministic sorted filename order (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_dir_sorted_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_dir_sorted_order_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_sorted_order_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_dir_sorted_order_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_sorted_order_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_z_directtests_sorted_tests.mla    ${code}
    Create File    ${suite_dir}/test_a_directtests_sorted_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Match Regexp    ${log_text}    (?s).*--tests ${suite_dir}/test_a_directtests_sorted_tests\\.mla.*--tests ${suite_dir}/test_z_directtests_sorted_tests\\.mla.*

MLang Frontend DirectTests Directory Skips Synthetic Test Root Files
    [Documentation]    Verify direct `--tests <dir>` ignores __mlang_test_root.mla while still running valid test_ suites (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_skiproot
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_skiproot_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_skiproot_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_skiproot_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bad_root}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_not_run_root() -> i32 {
    ...        return 1;
    ...    }
    ${good_test}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_run_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/__mlang_test_root.mla    ${bad_root}
    Create File    ${suite_dir}/test_ok_tests.mla    ${good_test}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_ok_tests.mla
    Should Not Contain    ${log_text}    __mlang_test_root.mla

MLang Frontend DirectTests Skips Modonly Root Candidate
    [Documentation]    Verify direct `--tests <dir>` includes only suite-style files and skips __mlang_test_root_modonly.mla.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_modonly
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_modonly_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_modonly_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_directtests_modonly_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${modonly}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn should_still_be_candidate_modonly() -> i32 {
    ...        return 0;
    ...    }
    ${good_test}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn directtests_modonly_companion() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/__mlang_test_root_modonly.mla    ${modonly}
    Create File    ${suite_dir}/test_directtests_modonly_ok.mla    ${good_test}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    --tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    __mlang_test_root_modonly.mla
    Should Contain    ${log_text}    --tests ${suite_dir}/test_directtests_modonly_ok.mla

MLang Frontend Bench DefaultPath Ignores NoRun
    [Documentation]    Verify `bench --no-run` without explicit path defaults to tests/ and does not forward --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_default_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_norun_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench tests/bench_
    Should Not Contain    ${log_text}    --no-run

MLang Frontend Bench DefaultPath Ignores ColonWarningFlags
    [Documentation]    Verify `bench -Wno-colon-if/-Wno-colon-while` without explicit path defaults to tests/ and does not forward those flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_default_nowarn
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_nowarn_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_nowarn_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    -Wno-colon-if    -Wno-colon-while
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench tests/bench_
    Should Not Contain    ${log_text}    -Wno-colon-if
    Should Not Contain    ${log_text}    -Wno-colon-while

MLang Frontend Bench DefaultPath Forwards LinkerFlags
    [Documentation]    Verify `bench -L<dir> -l<name>` without explicit path defaults to tests/ and forwards linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_default_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_default_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    -L/tmp/mlang_default_bench_lib    -ldefaultbenchdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench tests/bench_
    Should Contain    ${log_text}    -L/tmp/mlang_default_bench_lib
    Should Contain    ${log_text}    -ldefaultbenchdep

MLang Frontend RunTests Directory Forwards NoRun
    [Documentation]    Verify `run tests <dir> --no-run` forwards --no-run to each suite invocation.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_norun_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn run_tests_norun_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_norun_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    run    tests    ${suite_dir}    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --no-run

MLang Frontend RunTests Directory NoRunThenTests Does Not Forward NoRun
    [Documentation]    Verify `run tests <dir> --no-run --tests` clears no-run and does not forward --no-run per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_norun_then_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_norun_then_tests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_norun_then_tests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_norun_then_tests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_norun_then_tests_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_norun_then_tests_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    --no-run    --tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_norun_then_tests_tests.mla
    Should Not Contain    ${log_text}    --no-run

MLang Frontend RunTests Directory TestsThenNoRun Forwards NoRun
    [Documentation]    Verify `run tests <dir> --tests --no-run` keeps no-run and forwards --no-run per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_tests_then_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_tests_then_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_tests_then_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_tests_then_norun_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_tests_then_norun_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_tests_then_norun_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_tests_then_norun_tests.mla
    Should Contain    ${log_text}    --no-run

MLang Frontend Test Directory NoRunThenTests Does Not Forward NoRun
    [Documentation]    Verify `test <dir> --no-run --tests` clears no-run and does not forward --no-run per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_dir_norun_then_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_dir_norun_then_tests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_dir_norun_then_tests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_dir_norun_then_tests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_dir_norun_then_tests_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_test_dir_norun_then_tests_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${suite_dir}    --no-run    --tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_test_dir_norun_then_tests_tests.mla
    Should Not Contain    ${log_text}    --no-run

MLang Frontend Test Directory TestsThenNoRun Forwards NoRun
    [Documentation]    Verify `test <dir> --tests --no-run` keeps no-run and forwards --no-run per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_dir_tests_then_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_dir_tests_then_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_dir_tests_then_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_dir_tests_then_norun_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_dir_tests_then_norun_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_test_dir_tests_then_norun_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${suite_dir}    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_test_dir_tests_then_norun_tests.mla
    Should Contain    ${log_text}    --no-run

MLang Frontend SingleFile Forwards NoTests
    [Documentation]    Verify single-file test mode forwards `--no-tests` to backend (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_notests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_notests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_notests_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${src}    --no-tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${src}
    Should Contain    ${log_text}    --no-tests

MLang Frontend Test SingleFile Forwards NoTests
    [Documentation]    Verify `test <file> --no-tests` forwards --no-tests in single-file test mode (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_single_notests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_single_notests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_single_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_single_notests_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${src}    --no-tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${src}
    Should Contain    ${log_text}    --no-tests

MLang Frontend RunTests SingleFile Forwards NoTests
    [Documentation]    Verify `run tests <file> --no-tests` forwards --no-tests in single-file test mode (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_single_notests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_single_notests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_single_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_single_notests_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${src}    --no-tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${src}
    Should Contain    ${log_text}    --no-tests

MLang Frontend Directory Mode Does Not Forward NoTests
    [Documentation]    Verify directory test mode does not forward `--no-tests` into per-suite invocations (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_notests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_notests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_notests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn dir_mode_notests_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_dir_notests_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    test    --no-tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_dir_notests_tests.mla
    Should Not Contain    ${log_text}    --no-tests

MLang Frontend RunTests Directory Mode Does Not Forward NoTests
    [Documentation]    Verify `run tests <dir> --no-tests` does not forward `--no-tests` into per-suite invocations (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_notests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_notests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_notests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_mode_notests_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_notests_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    run    tests    --no-tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_notests_tests.mla
    Should Not Contain    ${log_text}    --no-tests

MLang Frontend RunTests Directory Ignores CompileFlags In TestMode
    [Documentation]    Verify `run tests <dir>` strips compile-only flags from per-suite backend calls (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_compileflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_compileflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_compileflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_compileflags_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_compile_flags_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_compileflags_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    -c    -S    -emit-llvm    -emit-bc    -O3    -v    --debug
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_compileflags_tests.mla
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-c( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-S( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-emit-llvm( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-emit-bc( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-O3( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-v( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )--debug( |$).*

MLang Frontend RunTests Directory Forwards ColonWarningFlags
    [Documentation]    Verify `run tests <dir> -Wno-colon-*` forwards colon warning suppression flags per suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_colonflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_colonflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_colonflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_colonflags_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_colon_flags_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_colonflags_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    -Wno-colon-if    -Wno-colon-while
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_colonflags_tests.mla
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while

MLang Frontend RunTests Directory Forwards Split LinkerFlags
    [Documentation]    Verify `run tests <dir> -L <dir> -l <name>` forwards linker flags per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_split_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_split_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_split_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_split_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_split_link_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_split_link_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    -L    /tmp/mlang_run_tests_dir_link    -l    mlangruntests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_split_link_tests.mla
    Should Contain    ${log_text}    -L /tmp/mlang_run_tests_dir_link
    Should Contain    ${log_text}    -l mlangruntests

MLang Frontend RunTests Directory Uses Sorted Suite Order
    [Documentation]    Verify `run tests <dir>` executes suites in deterministic sorted filename order (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_sorted_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_sorted_order_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_sorted_order_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_sorted_order_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn sorted_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_z_sorted_tests.mla    ${code}
    Create File    ${suite_dir}/test_a_sorted_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    run    tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Match Regexp    ${log_text}    (?s).*--tests ${suite_dir}/test_a_sorted_tests\\.mla.*--tests ${suite_dir}/test_z_sorted_tests\\.mla.*

MLang Frontend RunTests Directory Forwards Compact LinkerFlags
    [Documentation]    Verify `run tests <dir> -Lfoo -lbar -Wl,...` forwards compact linker flags per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_compact_link
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_compact_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_compact_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_compact_link_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_compact_link_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    -L/tmp/mlang_run_tests_dir_compact    -lmlangruncompact    -Wl,-rpath,/tmp/mlang_run_tests_dir_compact
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_compact_link_tests.mla
    Should Contain    ${log_text}    -L/tmp/mlang_run_tests_dir_compact
    Should Contain    ${log_text}    -lmlangruncompact
    Should Contain    ${log_text}    -Wl,-rpath,/tmp/mlang_run_tests_dir_compact

MLang Frontend RunTests Directory Ignores OutputFlag
    [Documentation]    Verify `run tests <dir> -o <file>` is ignored in directory mode (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_ignore_o
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_ignore_o_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_ignore_o_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_ignore_o_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_ignore_o_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_ignore_o_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    run    tests    ${suite_dir}    -o    ${ARTIFACT DIR}/ignored_runtests_dir_bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_ignore_o_tests.mla
    Should Not Contain    ${log_text}    -o
    Should Not Contain    ${log_text}    ignored_runtests_dir_bin

MLang Frontend Test SingleFile Forwards BenchTuningFlags
    [Documentation]    Verify test single-file mode forwards --bench-iters/--bench-warmup (accepted by C++ parser in testMode).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_single_benchflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_single_benchflags.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_single_benchflags_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_single_benchflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_test_single_benchflags_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${src}    --bench-iters    12    --bench-warmup    3
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${src}
    Should Contain    ${log_text}    --bench-iters 12
    Should Contain    ${log_text}    --bench-warmup 3

MLang Frontend RunTests Directory Ignores BenchTuningFlags
    [Documentation]    Verify `run tests <dir>` accepts but does not forward --bench-iters/--bench-warmup (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_benchflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_benchflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_benchflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_runtests_dir_benchflags_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_benchflags_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_benchflags_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    run    tests    ${suite_dir}    --bench-iters    12    --bench-warmup    3
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_runtests_dir_benchflags_tests.mla
    Should Not Contain    ${log_text}    --bench-iters
    Should Not Contain    ${log_text}    --bench-warmup

MLang Frontend Test Invalid BenchIters Value Errors
    [Documentation]    Verify test mode reports invalid numeric value for --bench-iters (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_invalid_benchiters
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_invalid_benchiters.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_invalid_benchiters_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --bench-iters    nope    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-iters

MLang Frontend RunTests Invalid BenchWarmup Value Errors
    [Documentation]    Verify run tests mode reports invalid numeric value for --bench-warmup (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_invalid_benchwarmup
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_invalid_benchwarmup.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_invalid_benchwarmup_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --bench-warmup    nope    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-warmup

MLang Frontend Test Missing BenchIters Value Uses Unknown option Error
    [Documentation]    Verify test mode missing value after --bench-iters reports unknown option (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_missing_benchiters
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    test    --bench-iters
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters

MLang Frontend RunTests Missing BenchWarmup Value Uses Unknown option Error
    [Documentation]    Verify run tests mode missing value after --bench-warmup reports unknown option (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_missing_benchwarmup
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    run    tests    --bench-warmup
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-warmup

MLang Frontend Test Inline Bench Flags Are Rejected
    [Documentation]    Verify test mode rejects inline --bench-iters=N/--bench-warmup=N as unknown options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_inline_benchflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_inline_benchflags.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_inline_benchflags_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${iters}=    Set Variable    --bench-iters=20
    ${warmup}=    Set Variable    --bench-warmup=5
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${src}    ${iters}    ${warmup}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters=20
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests Inline Bench Flags Are Rejected
    [Documentation]    Verify run tests mode rejects inline --bench-iters=N/--bench-warmup=N as unknown options (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_inline_benchflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_inline_benchflags.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_inline_benchflags_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${iters}=    Set Variable    --bench-iters=20
    ${warmup}=    Set Variable    --bench-warmup=5
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    run    tests    ${src}    ${iters}    ${warmup}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters=20
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests Directory Invalid BenchIters Value Errors
    [Documentation]    Verify `run tests <dir>` reports invalid numeric value for --bench-iters (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_invalid_benchiters
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_invalid_benchiters_suite
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_invalid_benchiters_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_invalid_benchiters_tests.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    ${suite_dir}    --bench-iters    nope
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-iters

MLang Frontend Test Directory Invalid BenchWarmup Value Errors
    [Documentation]    Verify `test <dir>` reports invalid numeric value for --bench-warmup (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_dir_invalid_benchwarmup
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_dir_invalid_benchwarmup_suite
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_dir_invalid_benchwarmup_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_test_dir_invalid_benchwarmup_tests.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    ${suite_dir}    --bench-warmup    nope
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-warmup

MLang Frontend RunTests Directory Missing BenchIters Value Uses Unknown option Error
    [Documentation]    Verify `run tests <dir> --bench-iters` missing value reports unknown option (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_dir_missing_benchiters
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_dir_missing_benchiters_suite
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_dir_missing_benchiters_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_runtests_dir_missing_benchiters_tests.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    ${suite_dir}    --bench-iters
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters

MLang Frontend Test Directory Missing BenchWarmup Value Uses Unknown option Error
    [Documentation]    Verify `test <dir> --bench-warmup` missing value reports unknown option (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_dir_missing_benchwarmup
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_dir_missing_benchwarmup_suite
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn test_dir_missing_benchwarmup_case() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_test_dir_missing_benchwarmup_tests.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    ${suite_dir}    --bench-warmup
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-warmup

MLang Frontend SingleFile Forwards CompileFlags In TestMode
    [Documentation]    Verify single-file test mode forwards compile-related flags (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_compileflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_compileflags.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn compile_flags_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_compileflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_compileflags_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    --tests    ${src}    -c    -S    -emit-llvm    -emit-bc    -O3    -v    --debug
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${src}
    Should Contain    ${log_text}    -c
    Should Contain    ${log_text}    -S
    Should Contain    ${log_text}    -emit-llvm
    Should Contain    ${log_text}    -emit-bc
    Should Contain    ${log_text}    -O3
    Should Contain    ${log_text}    -v
    Should Contain    ${log_text}    --debug

MLang Frontend Directory Mode Ignores CompileFlags In TestMode
    [Documentation]    Verify directory test mode strips compile-only flags from per-suite backend calls (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_compileflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_compileflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_compileflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_compileflags_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn dir_compile_flags_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_dir_compileflags_tests.mla    ${code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${suite_dir}    -c    -S    -emit-llvm    -emit-bc    -O3    -v    --debug
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests ${suite_dir}/test_dir_compileflags_tests.mla
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-c( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-S( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-emit-llvm( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-emit-bc( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-O3( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )-v( |$).*
    Should Not Match Regexp    ${log_text}    (?s).*(^| )--debug( |$).*

MLang Frontend Tests Flag Works In Trailing Position
    [Documentation]    Verify `<file> --tests` activates test mode even when --tests is not the first arg.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_flag_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    ${src}    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend failed to honor trailing --tests flag (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

MLang Frontend Trailing Tests Flag Unknown option Fails
    [Documentation]    Verify C++ parity: trailing --tests stream still rejects unknown options with usage.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_unknown
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_unknown.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_flag_unknown_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --tests    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests SingleFile Forwards OutputOption
    [Documentation]    Verify C++ parity: in trailing --tests single-file mode, -o <file> is forwarded.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_single_output
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_single_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_single_output_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_single_output_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_single_output_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    -o    trailing_test_bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    ${src}
    Should Contain    ${log_text}    -o trailing_test_bin
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while

MLang Frontend Trailing Tests SingleFile Forwards LinkFlags
    [Documentation]    Verify C++ parity: in trailing --tests single-file mode, -L/-l are forwarded.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_single_linkflags
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_single_linkflags.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_single_linkflags_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_single_linkflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_single_linkflags_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    -L    /tmp/mlang_trailing_lib    -l    trailingdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    ${src}
    Should Contain    ${log_text}    -L /tmp/mlang_trailing_lib
    Should Contain    ${log_text}    -l trailingdep

MLang Frontend Trailing Tests SingleFile MissingLinkOrOutputValue Fails
    [Documentation]    Verify C++ parity: trailing --tests single-file mode reports unknown option for missing -o/-L/-l values.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_single_missing_link_or_output
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_single_missing_link_or_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_single_missing_link_or_output_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}

    ${run_o}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --tests    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_o.rc}    0
    Should Contain    ${run_o.stderr}    Unknown option: -o
    Should Contain    ${run_o.stdout}    Usage:

    ${run_L}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --tests    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_L.rc}    0
    Should Contain    ${run_L.stderr}    Unknown option: -L
    Should Contain    ${run_L.stdout}    Usage:

    ${run_l}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --tests    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_l.rc}    0
    Should Contain    ${run_l.stderr}    Unknown option: -l
    Should Contain    ${run_l.stdout}    Usage:

MLang Frontend Trailing Tests Help Before Unknown Succeeds
    [Documentation]    Verify C++ parity: in trailing --tests stream, --help before unknown option short-circuits successfully.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_help_order.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_help_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests Unknown Before Help Fails
    [Documentation]    Verify C++ parity: in trailing --tests stream, unknown option before --help fails.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_unknown_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_unknown_help_order.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_unknown_help_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests Version Before Unknown Succeeds
    [Documentation]    Verify C++ parity: in trailing --tests stream, --version before unknown option short-circuits successfully.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_version_order.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_version_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Trailing Tests Unknown Before Version Fails
    [Documentation]    Verify C++ parity: in trailing --tests stream, unknown option before --version fails.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_unknown_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_unknown_version_order.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_unknown_version_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --definitely-unknown-flag    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests ShortHelp Before Unknown Succeeds
    [Documentation]    Verify C++ parity: in trailing --tests stream, -h before unknown option short-circuits successfully.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_shorthelp_order.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_shorthelp_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests Unknown Before ShortHelp Fails
    [Documentation]    Verify C++ parity: in trailing --tests stream, unknown option before -h fails.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_unknown_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_unknown_shorthelp_order.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_unknown_shorthelp_order_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --definitely-unknown-flag    -h
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests Help ShortCircuits MissingValueOption
    [Documentation]    Verify C++ parity: in trailing --tests stream, --help short-circuits and ignores trailing missing-value options.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_help_missing_value
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_help_missing_value.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_help_missing_value_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --help    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests Version ShortCircuits MissingValueOption
    [Documentation]    Verify C++ parity: in trailing --tests stream, --version short-circuits and ignores trailing missing-value options.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_version_missing_value
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_version_missing_value.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_version_missing_value_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    --version    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Trailing Tests ShortHelp ShortCircuits MissingValueOption
    [Documentation]    Verify C++ parity: in trailing --tests stream, -h short-circuits and ignores trailing missing-value options.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_shorthelp_missing_value
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_shorthelp_missing_value.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_shorthelp_missing_value_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    ${src}    --tests    -h    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Trailing Tests Flag Injects Default Colon Suppression
    [Documentation]    Verify C++ parity: trailing --tests in compile stream injects default -Wno-colon-if/-Wno-colon-while.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_colon_defaults
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_colon_defaults.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_colon_defaults_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_colon_defaults_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_colon_defaults_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    ${src}    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    --no-run
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while

MLang Frontend Trailing Tests Flag Does Not Duplicate Explicit Colon Suppression
    [Documentation]    Verify C++ parity: explicit -Wno-colon-* in trailing --tests stream are preserved without duplicate injection.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_colon_nodup
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_colon_nodup.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_colon_nodup_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_colon_nodup_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_colon_nodup_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    -Wno-colon-if    -Wno-colon-while
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while
    Should Not Match Regexp    ${log_text}    (?s).*-Wno-colon-if.*-Wno-colon-if.*
    Should Not Match Regexp    ${log_text}    (?s).*-Wno-colon-while.*-Wno-colon-while.*

MLang Frontend Trailing Tests Flag Injects Only Missing Colon Suppression
    [Documentation]    Verify C++ parity: trailing --tests with one explicit -Wno-colon-* injects only the missing counterpart.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_tests_trailing_colon_partial
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_tests_trailing_colon_partial.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn trailing_colon_partial_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_colon_partial_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_tests_trailing_colon_partial_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    -Wno-colon-if
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while
    Should Not Match Regexp    ${log_text}    (?s).*-Wno-colon-if.*-Wno-colon-if.*
    Should Not Match Regexp    ${log_text}    (?s).*-Wno-colon-while.*-Wno-colon-while.*

MLang Binary Frontend Env Switch Works
    [Documentation]    Verify `MLANG_FRONTEND_IMPL=mla` routes `mlang` through the MLang frontend implementation.
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_env_switch.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_env_switch_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        println!("frontend env switch ok");
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}
    ...    env:MLANG_FRONTEND_IMPL=mla    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=MLANG_FRONTEND_IMPL=mla build failed (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    frontend env switch ok

MLang Frontend Wrapper Pkg Dispatch Works
    [Documentation]    Verify mlang-frontend-mla handles `pkg` command path with MLANG_PKG_IMPL=cpp by forwarding directly to backend.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ...    msg=Failed building frontend wrapper (pkg dispatch) (rc=${build_front.rc})\nSTDOUT:\n${build_front.stdout}\nSTDERR:\n${build_front.stderr}
    ${r1}=    Run Process    ${frontend}    --backend    /bin/echo    pkg    init
    ...    env:MLANG_PKG_IMPL=cpp    env:MLANG_FRONTEND_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${r1.rc}    0
    ...    msg=frontend pkg init with cpp backend failed (rc=${r1.rc})\nSTDOUT:\n${r1.stdout}\nSTDERR:\n${r1.stderr}
    Should Contain    ${r1.stdout}    pkg init

MLang Frontend Pkg Unknown Impl Uses Cpp Fallback
    [Documentation]    Verify MLANG_PKG_IMPL unknown values do not prefer MLang pkg frontend (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_unknown
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /bin/echo    pkg    init
    ...    env:MLANG_PKG_IMPL=unknown    env:MLANG_FRONTEND_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=unknown MLANG_PKG_IMPL should route pkg to backend directly (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pkg init

MLang Frontend Pkg Mla Mode Falls Back To Cpp Backend
    [Documentation]    Verify `MLANG_PKG_IMPL=mla` falls back to backend `pkg` command when MLang pkg frontend compilation/run path fails.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_mla_fallback
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /bin/echo    pkg    init
    ...    env:MLANG_PKG_IMPL=mla    env:MLANG_FRONTEND_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=MLANG_PKG_IMPL=mla should fall back to backend passthrough on mla frontend failure (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pkg init

MLang Frontend Pkg Default Mode Falls Back To Cpp Backend
    [Documentation]    Verify default pkg mode (MLANG_PKG_IMPL unset) falls back to backend `pkg` command when MLang pkg frontend compilation/run path fails.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_default_fallback
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /bin/echo    pkg    init
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=default pkg mode should fall back to backend passthrough on mla frontend failure (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pkg init

MLang Frontend Pkg Cpp Fallback Forwards Full Argument Vector
    [Documentation]    Verify cpp pkg fallback preserves full pkg argument ordering/content.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_cpp_args
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /bin/echo
    ...    pkg    add    mydep    --git    https://example.com/repo.git    --rev    abc123    --tag    v1.2.3    --pkg-config    zlib    --system
    ...    env:MLANG_PKG_IMPL=unknown    env:MLANG_FRONTEND_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Match Regexp    ${run.stdout}    (?s).*pkg add mydep --git https://example\\.com/repo\\.git --rev abc123 --tag v1\\.2\\.3 --pkg-config zlib --system.*

MLang Frontend Pkg MlaFallback Forwards Full Argument Vector
    [Documentation]    Verify preferred mla pkg path fallback still preserves full pkg argument ordering/content.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_mla_args
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /bin/echo
    ...    pkg    add    mydep    --git    https://example.com/repo.git    --rev    abc123    --tag    v1.2.3    --pkg-config    zlib    --system
    ...    env:MLANG_PKG_IMPL=mla    env:MLANG_FRONTEND_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Match Regexp    ${run.stdout}    (?s).*pkg add mydep --git https://example\\.com/repo\\.git --rev abc123 --tag v1\\.2\\.3 --pkg-config zlib --system.*

MLang Frontend Pkg Mla Mode Routes Under FrontendImplMla Env
    [Documentation]    Verify pkg command under `MLANG_FRONTEND_IMPL=mla` does not silently succeed.
    ...    Accept either direct pkg-mla unknown-subcommand output or compile/fallback error output.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_mla_env
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${repo_root}=    Catenate    SEPARATOR=    ${CURDIR}/../..
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    pkg    --help
    ...    env:MLANG_PKG_IMPL=mla    env:MLANG_FRONTEND_IMPL=mla    cwd=${repo_root}    timeout=20s    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend pkg --help under MLANG_FRONTEND_IMPL=mla should fail with nonzero (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    ${has_unknown}=    Run Keyword And Return Status    Should Contain    ${run.stderr}    Unknown pkg subcommand: --help
    ${has_compile_error}=    Run Keyword And Return Status    Should Contain    ${run.stderr}    Compilation failed due to errors.
    ${has_unwrap_warn}=    Run Keyword And Return Status    Should Contain    ${run.stderr}    result.unwrap() may panic
    ${has_usage}=    Run Keyword And Return Status    Should Contain    ${run.stdout}    Usage:
    ${ok}=    Evaluate    bool(${has_unknown} or ${has_compile_error} or ${has_unwrap_warn} or ${has_usage})
    Should Be True    ${ok}

MLang Frontend Pkg Mla Mode Reuses Cached Frontend Binary
    [Documentation]    Regression: verify pkg-mla frontend compilation is cached and not repeated for unchanged source/backend/cache key.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_pkg_mla_cache_reuse
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${repo_root}=    Catenate    SEPARATOR=    ${CURDIR}/../..
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_mlang_pkg_cache_wrapper.sh
    ${backend_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_mlang_pkg_cache_wrapper.log
    ${pkg_run_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_mlang_pkg_cache_run.log
    ${cache_key}=    Evaluate    "pkg_cache_reuse_" + str(__import__('time').time_ns())
    Run Keyword And Ignore Error    Remove File    ${backend_log}
    Run Keyword And Ignore Error    Remove File    ${pkg_run_log}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${backend_log}"
    ...    if [ "$1" = "tools/mlang-pkg-mla/main.mla" ]; then
    ...      exit 1
    ...    fi
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${bootstrap}=    Run Process    ${frontend}    --backend    ${fake_backend}    pkg    --help
    ...    env:MLANG_PKG_IMPL=mla    env:MLANG_FRONTEND_IMPL=cpp    env:MLANG_PKG_CACHE_KEY=${cache_key}
    ...    cwd=${repo_root}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${bootstrap.rc}    0
    ${bootstrap_log}=    Get File    ${backend_log}
    ${cached_bin}=    Evaluate    __import__("re").search(r"-o\\s+(\\S+)", """${bootstrap_log}""").group(1)
    ${cached_script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${pkg_run_log}"
    ...    exit 0
    Create File    ${cached_bin}    ${cached_script}
    ${chmod_cached}=    Run Process    /bin/sh    -lc    chmod +x "${cached_bin}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod_cached.rc}    0
    Run Keyword And Ignore Error    Remove File    ${backend_log}
    ${run1}=    Run Process    ${frontend}    --backend    ${fake_backend}    pkg    --help
    ...    env:MLANG_PKG_IMPL=mla    env:MLANG_FRONTEND_IMPL=cpp    env:MLANG_PKG_CACHE_KEY=${cache_key}
    ...    cwd=${repo_root}    stdout=PIPE    stderr=PIPE
    ${run2}=    Run Process    ${frontend}    --backend    ${fake_backend}    pkg    --help
    ...    env:MLANG_PKG_IMPL=mla    env:MLANG_FRONTEND_IMPL=cpp    env:MLANG_PKG_CACHE_KEY=${cache_key}
    ...    cwd=${repo_root}    stdout=PIPE    stderr=PIPE
    ${backend_exists}=    Run Keyword And Return Status    File Should Exist    ${backend_log}
    Should Be Equal    ${backend_exists}    ${False}
    ${backend_text}=    Set Variable    ${EMPTY}
    ${compile_count}=    Evaluate    """${backend_text}""".count("tools/mlang-pkg-mla/main.mla")
    Should Be Equal As Integers    ${compile_count}    0

MLang Frontend Empty Test Dir Fails
    [Documentation]    Verify frontend parity with C++ main: `test <empty_dir>` returns nonzero.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_emptydir
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ...    msg=Failed building frontend wrapper (empty dir parity) (rc=${build_front.rc})\nSTDOUT:\n${build_front.stdout}\nSTDERR:\n${build_front.stderr}
    ${empty}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_empty_suite_dir
    Create Directory    ${empty}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    test    ${empty}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend DirectTests Empty Test Dir Fails
    [Documentation]    Verify frontend parity with C++ main: `--tests <empty_dir>` returns nonzero.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_emptydir
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${empty}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_empty_suite_dir
    Run Keyword And Ignore Error    Remove Directory    ${empty}    recursive=True
    Create Directory    ${empty}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    --tests    ${empty}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend Test Dir Requires Test Attribute
    [Documentation]    Verify test directory mode ignores files lacking #[test] and errors when no test suites remain.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_test_attr
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_test_attr_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${plain}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_no_attr_tests.mla    ${plain}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    test    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend DirectTests Dir Requires Test Attribute
    [Documentation]    Verify direct --tests directory mode ignores files lacking #[test] and errors when no test suites remain.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_attr
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_attr_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${plain}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_no_attr_tests.mla    ${plain}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    --tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend Empty Bench Dir Fails
    [Documentation]    Verify frontend parity with C++ main: `bench <empty_dir>` returns nonzero.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_emptybench
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ...    msg=Failed building frontend wrapper (empty bench dir parity) (rc=${build_front.rc})\nSTDOUT:\n${build_front.stdout}\nSTDERR:\n${build_front.stderr}
    ${empty}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_empty_bench_dir
    Run Keyword And Ignore Error    Remove Directory    ${empty}    recursive=True
    Create Directory    ${empty}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    bench    ${empty}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla benchmark files found in

MLang Frontend Bench Dir Requires Bench Prefix
    [Documentation]    Verify bench directory mode only considers `bench_*.mla` files (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_prefix
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_prefix_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${nonbench}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_not_a_bench.mla    ${nonbench}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    bench    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla benchmark files found in

MLang Frontend Bench Directory Skips Synthetic Test Root Files
    [Documentation]    Verify bench directory mode ignores __mlang_test_root.mla while still running valid bench_ suites (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_skiproot
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_skiproot_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_skiproot_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_skiproot_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bad_root}=    Catenate    SEPARATOR=\n
    ...    \#[bench]
    ...    fn should_not_run_root() -> i32 {
    ...        return 1;
    ...    }
    ${good_bench}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/__mlang_test_root.mla    ${bad_root}
    Create File    ${suite_dir}/bench_ok.mla    ${good_bench}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench ${suite_dir}/bench_ok.mla
    Should Not Contain    ${log_text}    __mlang_test_root.mla

MLang Frontend Test Uses Last Positional Path
    [Documentation]    Verify frontend test-mode positional parsing matches C++ main semantics (last positional wins).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_lastpos
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_lastpos_suite
    ${empty_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_lastpos_empty
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Run Keyword And Ignore Error    Remove Directory    ${empty_dir}    recursive=True
    Create Directory    ${suite_dir}
    Create Directory    ${empty_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn frontend_lastpos_smoke() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_frontend_lastpos.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${suite_dir}    ${empty_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend RunTests Uses Last Positional Path
    [Documentation]    Verify frontend run-tests positional parsing matches C++ semantics (last positional wins).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_lastpos
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_lastpos_suite
    ${empty_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_lastpos_empty
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Run Keyword And Ignore Error    Remove Directory    ${empty_dir}    recursive=True
    Create Directory    ${suite_dir}
    Create Directory    ${empty_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn frontend_runtests_lastpos_smoke() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_frontend_runtests_lastpos.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    run    tests    ${suite_dir}    ${empty_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend DirectTests Uses Last Positional Path
    [Documentation]    Verify direct --tests positional parsing matches C++ semantics (last positional wins).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_directtests_lastpos
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_lastpos_suite
    ${empty_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_directtests_lastpos_empty
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Run Keyword And Ignore Error    Remove Directory    ${empty_dir}    recursive=True
    Create Directory    ${suite_dir}
    Create Directory    ${empty_dir}
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn frontend_directtests_lastpos_smoke() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_frontend_directtests_lastpos.mla    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    --tests    ${suite_dir}    ${empty_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend Skips Synthetic Test Root Files
    [Documentation]    Verify frontend test directory mode ignores __mlang_test_root*.mla files during suite discovery.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_skiproot
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_skip_root_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bad_root}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        this is invalid syntax
    ...    }
    Create File    ${suite_dir}/__mlang_test_root.mla    ${bad_root}
    ${ok_test}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn frontend_skip_root_smoke() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_frontend_skip_root.mla    ${ok_test}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    test    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend should ignore synthetic root files (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [SUITE PASS]

MLang Frontend Keeps Modonly Root Candidate
    [Documentation]    Verify frontend parity with C++: only __mlang_test_root.mla is skipped, modonly variant remains candidate.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_modonly
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_modonly_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${modonly_bad}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        this is invalid syntax
    ...    }
    Create File    ${suite_dir}/__mlang_test_root_modonly.mla    ${modonly_bad}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    test    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    ...    msg=modonly root file should still be considered and fail parse (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

MLang Frontend Normalizes Multi-Suite Failure Exit Code
    [Documentation]    Verify frontend test-directory mode returns rc=1 when multiple suites fail (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_failnorm
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_fail_norm_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bad1}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn bad_one() -> i32 {
    ...        return;
    ...    }
    ${bad2}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn bad_two() -> i32 {
    ...        return;
    ...    }
    Create File    ${suite_dir}/test_fail_one.mla    ${bad1}
    Create File    ${suite_dir}/test_fail_two.mla    ${bad2}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    test    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    1
    ...    msg=frontend should normalize multi-suite failures to rc=1 (got ${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [SUITE FAIL]

MLang Frontend Bench Flag Parsing Works
    [Documentation]    Verify frontend bench mode parses option values without treating them as input path.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_parse
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ...    msg=Failed building frontend wrapper (bench parse) (rc=${build_front.rc})\nSTDOUT:\n${build_front.stdout}\nSTDERR:\n${build_front.stderr}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    20    --bench-warmup    5    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend bench parse failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [BENCH]

MLang Frontend Supports Inline Asm Emit LLVM
    [Documentation]    Verify frontend forwards inline asm sources to the backend compiler and preserves volatile vs non-volatile LLVM IR lowering.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_inline_asm_emit
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_inline_asm_emit.mla
    ${ll}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_inline_asm_emit.ll
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let value: i64 = 9;
    ...        let copy: i64 = asm(i64, "", value);
    ...        asm volatile(void, "", value);
    ...        if copy != 9 {
    ...            return 1;
    ...        }
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    -emit-llvm    ${src}    -o    ${ll}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=frontend inline asm emit-llvm failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    ${ll_text}=    Get File    ${ll}
    Should Contain    ${ll_text}    call i64 asm
    Should Contain    ${ll_text}    call void asm sideeffect

MLang Frontend Supports Target Arch For Inline Asm
    [Documentation]    Verify frontend forwards --target-arch and surfaces inline asm arch mismatch errors.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_inline_asm_target_arch
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src_ok}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_inline_asm_aarch64_emit.mla
    ${ll_ok}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_inline_asm_aarch64_emit.ll
    ${code_ok}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let base: i64 = 4;
    ...        let delta: i64 = 5;
    ...        let sum: i64 = asm aarch64(i64, "add $0, $1, $2", base, delta);
    ...        if sum != 9 {
    ...            return 1;
    ...        }
    ...        return 0;
    ...    }
    Create File    ${src_ok}    ${code_ok}
    ${run_ok}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    --target-arch    aarch64    -emit-llvm    ${src_ok}    -o    ${ll_ok}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run_ok.rc}    0
    ${ll_ok_text}=    Get File    ${ll_ok}
    Should Contain    ${ll_ok_text}    target triple = "aarch64
    ${src_bad}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_inline_asm_arch_mismatch.mla
    ${code_bad}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let value: i64 = 9;
    ...        let copy: i64 = asm aarch64(i64, "", value);
    ...        if copy != 9 {
    ...            return 1;
    ...        }
    ...        return 0;
    ...    }
    Create File    ${src_bad}    ${code_bad}
    ${run_bad}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    --target-arch    x64    -emit-llvm    ${src_bad}    -o    ${ARTIFACT DIR}/frontend_inline_asm_arch_mismatch.ll
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_bad.rc}    0
    Should Contain    ${run_bad.stderr}    inline asm target arch 'aarch64' does not match compilation target arch 'x64'

MLang Frontend Bench Flag Validation
    [Documentation]    Verify frontend bench mode reports invalid numeric values for bench flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_validate
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    nope    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-iters

MLang Frontend Bench Inline Flags Are Rejected
    [Documentation]    Verify C++ parity: --bench-iters=N and --bench-warmup=N are unknown options.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_inline
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${iters}=    Set Variable    --bench-iters=20
    ${warmup}=    Set Variable    --bench-warmup=5
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${iters}    ${warmup}    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters=20
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench SingleFile Rejects Bare Wl Flag
    [Documentation]    Verify C++ parity: bare -Wl, is rejected in single-file bench mode as unknown.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_wl_bare
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_wl_bare.mla
    Create File    ${src}    fn main() -> i32 { return 0; }
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${src}    -Wl,
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: -Wl,

MLang Frontend Bench SingleFile Forwards Wl Flags
    [Documentation]    Verify C++ parity: single-file bench mode forwards valid -Wl,<args> linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_wl
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_wl.mla
    Create File    ${src}    fn main() -> i32 { return 0; }
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_wl_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_wl_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${src}    -Wl,-rpath,/tmp/mlang_bench_single
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -Wl,-rpath,/tmp/mlang_bench_single

MLang Frontend Bench SingleFile Forwards Compact Linker Flags
    [Documentation]    Verify C++ parity: single-file bench mode forwards compact -L<dir> and -l<name> flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_compact_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_compact_link.mla
    Create File    ${src}    fn main() -> i32 { return 0; }
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_compact_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${src}    -L/tmp/mlang_bench_single_lib    -lbenchsingledep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -L/tmp/mlang_bench_single_lib
    Should Contain    ${log_text}    -lbenchsingledep

MLang Frontend Bench SingleFile Forwards Split Linker Flags
    [Documentation]    Verify C++ parity: single-file bench mode forwards split -L <dir> and -l <name> flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_split_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_split_link.mla
    Create File    ${src}    fn main() -> i32 { return 0; }
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_split_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_split_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${src}    -L    /tmp/mlang_bench_single_split_lib    -l    benchsinglesplitdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -L /tmp/mlang_bench_single_split_lib
    Should Contain    ${log_text}    -l benchsinglesplitdep

MLang Frontend Bench SingleFile Forwards OutputFlag
    [Documentation]    Verify C++ parity: single-file bench mode forwards `-o <file>`.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_output.mla
    Create File    ${src}    fn main() -> i32 { return 0; }
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_output_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_output_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${src}    -o    bench_single_output_bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -o bench_single_output_bin

MLang Frontend Bench Warmup Validation
    [Documentation]    Verify frontend rejects non-numeric warmup values before backend compile.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_warmup_validate
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-warmup    nope    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-warmup

MLang Frontend Bench Numeric Range Validation
    [Documentation]    Verify frontend rejects out-of-range bench numeric values (std::stoi parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_range_validate
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${huge_pos}=    Set Variable    999999999999999999999999999999999999
    ${huge_neg}=    Set Variable    -999999999999999999999999999999999999

    ${run_i}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    ${huge_pos}    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_i.rc}    0
    Should Contain    ${run_i.stderr}    Invalid value for --bench-iters

    ${run_w}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-warmup    ${huge_neg}    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_w.rc}    0
    Should Contain    ${run_w.stderr}    Invalid value for --bench-warmup

MLang Frontend Bench Numeric I32 Boundary Behavior
    [Documentation]    Verify i32 boundary values are accepted and clamped/forwarded with C++-parity semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_i32_bounds
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_i32_bounds.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_i32_bounds_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_i32_bounds_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0

    ${run_max}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${bench_file}    --bench-iters    2147483647    --bench-warmup    2147483647
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run_max.rc}    0

    ${run_min}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${bench_file}    --bench-iters    -2147483648    --bench-warmup    -2147483648
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run_min.rc}    0

    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 2147483647
    Should Contain    ${log_text}    --bench-warmup 2147483647
    Should Contain    ${log_text}    --bench-iters 1
    Should Contain    ${log_text}    --bench-warmup 0

MLang Frontend Bench Accepts Signed Numeric Warmup
    [Documentation]    Verify frontend accepts signed numeric warmup values (backend clamps like C++).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_warmup_signed
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    5    --bench-warmup    -1    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0

MLang Frontend Bench Accepts Zero Iterations Value
    [Documentation]    Verify frontend accepts numeric zero for --bench-iters (backend applies C++ clamp).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_iters_zero
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    0    --bench-warmup    0    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0

MLang Frontend Bench Uses Last Positional Path
    [Documentation]    Verify C++ parity: in bench mode, the last non-flag positional argument wins as input path.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_last_pos
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${first}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_last_first.mla
    ${second}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_last_second.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_last_pos_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_last_pos_backend.log
    Create File    ${first}    fn main() -> i32 { return 0; }
    Create File    ${second}    fn main() -> i32 { return 0; }
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${first}    ${second}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench ${second}
    Should Not Contain    ${log_text}    bench ${first}

MLang Frontend Bench Directory Forwards Default Iteration Flags
    [Documentation]    Verify frontend bench directory mode forwards C++ default --bench-iters/--bench-warmup when not provided.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_defaults
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_defaults_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 100000
    Should Contain    ${log_text}    --bench-warmup 10000
    Should Not Contain    ${log_text}    -lmlang_std

MLang Frontend Bench SingleFile Does Not Inject Default Iteration Flags
    [Documentation]    Verify single-file bench mode does not inject default --bench-iters/--bench-warmup (backend handles defaults).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_defaults
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_defaults.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_defaults_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_defaults_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    ${bench_file}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    --bench-iters 100000
    Should Not Contain    ${log_text}    --bench-warmup 10000

MLang Frontend Bench SingleFile Clamps Iteration Values
    [Documentation]    Verify bench single-file mode clamps --bench-iters to >=1 and --bench-warmup to >=0 before forwarding.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_clamp_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_clamp_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_clamp_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_clamp_single_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${bench_file}    --bench-iters    0    --bench-warmup    -7
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 1
    Should Contain    ${log_text}    --bench-warmup 0

MLang Frontend Bench Directory Uses Last Iteration Flags
    [Documentation]    Verify bench directory mode forwards only one effective bench-iters/warmup pair and last value wins.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_lastflags
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_lastflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_lastflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_lastflags_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    --bench-iters    11    --bench-warmup    3    --bench-iters    22    --bench-warmup    7
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 22
    Should Contain    ${log_text}    --bench-warmup 7
    Should Not Contain    ${log_text}    --bench-iters 11
    Should Not Contain    ${log_text}    --bench-warmup 3

MLang Frontend Bench Directory Canonicalizes Numeric Flags
    [Documentation]    Verify bench directory mode forwards canonical numeric forms (e.g. +0007 -> 7).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_canon
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_canon_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_canon_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_canon_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    --bench-iters    +0007    --bench-warmup    +0000
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 7
    Should Contain    ${log_text}    --bench-warmup 0
    Should Not Contain    ${log_text}    +0007
    Should Not Contain    ${log_text}    +0000

MLang Frontend Bench Directory Clamps Iteration Values
    [Documentation]    Verify bench directory mode clamps --bench-iters to >=1 and --bench-warmup to >=0 before forwarding.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_clamp
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_clamp_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_clamp_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_clamp_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    --bench-iters    0    --bench-warmup    -7
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 1
    Should Contain    ${log_text}    --bench-warmup 0

MLang Frontend Bench Ignores NoRun Flag
    [Documentation]    Verify frontend bench mode ignores --no-run (C++ parity) and does not forward it.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_norun
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    --no-run    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    --no-run

MLang Frontend Bench SingleFile Forwards NoRun Flag
    [Documentation]    Verify bench single-file mode forwards --no-run (directory mode still ignores it).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_norun_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_norun_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_single_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    --no-run    ${bench_file}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --no-run

MLang Frontend Bench SingleFile NoRunWithTests StillForwards
    [Documentation]    Verify bench single-file keeps --no-run effective even when --tests also appears.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_norun_tests_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_norun_tests_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_tests_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_tests_single_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    --no-run    --tests    ${bench_file}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --no-run

MLang Frontend Bench SingleFile TestsThenNoRun StillForwards
    [Documentation]    Verify bench single-file keeps --no-run effective when flags are ordered as --tests then --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_tests_norun_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_tests_norun_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_tests_norun_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_tests_norun_single_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    --tests    --no-run    ${bench_file}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --no-run

MLang Frontend Bench SingleFile Forwards NoTests
    [Documentation]    Verify bench single-file mode forwards --no-tests (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_single_notests
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_single_notests.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_single_notests_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${bench_file}    --no-tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --no-tests

MLang Frontend Bench Directory NoRunWithTests StillIgnored
    [Documentation]    Verify bench directory mode still ignores --no-run even when --tests also appears.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_norun_tests_dir
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_norun_tests_dir_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_tests_dir_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_norun_tests_dir_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    --no-run    --tests    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    --no-run

MLang Frontend Bench Directory TestsThenNoRun StillIgnored
    [Documentation]    Verify bench directory mode still ignores --no-run when flags are ordered as --tests then --no-run.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_tests_norun_dir
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_tests_norun_dir_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_tests_norun_dir_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_tests_norun_dir_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    --tests    --no-run    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    --no-run

MLang Frontend Bench Directory Ignores NoTests
    [Documentation]    Verify bench directory mode ignores --no-tests and does not forward it per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_dir_notests
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_dir_notests_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_notests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_notests_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    --no-tests
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    --no-tests

MLang Frontend Bench Directory Forwards Split LinkerFlags
    [Documentation]    Verify bench directory mode forwards split -L/-l flags per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_dir_split_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_dir_split_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_split_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_split_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    -L    /tmp/mlang_bench_dir_split_lib    -l    benchdirsplitdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench ${suite_dir}/bench_case.mla
    Should Contain    ${log_text}    -L /tmp/mlang_bench_dir_split_lib
    Should Contain    ${log_text}    -l benchdirsplitdep

MLang Frontend Bench Directory Uses Sorted Suite Order
    [Documentation]    Verify `bench <dir>` executes suites in deterministic sorted filename order (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_dir_sorted_order
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_dir_sorted_order_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_sorted_order_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_sorted_order_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_z_sorted.mla    ${bench_code}
    Create File    ${suite_dir}/bench_a_sorted.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Match Regexp    ${log_text}    (?s).*bench ${suite_dir}/bench_a_sorted\\.mla.*bench ${suite_dir}/bench_z_sorted\\.mla.*

MLang Frontend Bench Directory Forwards Compact LinkerFlags
    [Documentation]    Verify bench directory mode forwards compact -L/-l and -Wl flags per-suite (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_dir_compact_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_dir_compact_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_compact_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    -L/tmp/mlang_bench_dir_compact_lib    -lbenchdircompactdep    -Wl,-rpath,/tmp/mlang_bench_dir_compact_lib
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench ${suite_dir}/bench_case.mla
    Should Contain    ${log_text}    -L/tmp/mlang_bench_dir_compact_lib
    Should Contain    ${log_text}    -lbenchdircompactdep
    Should Contain    ${log_text}    -Wl,-rpath,/tmp/mlang_bench_dir_compact_lib

MLang Frontend Bench Directory Ignores OutputFlag
    [Documentation]    Verify bench directory mode ignores `-o <file>` (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_dir_ignore_o
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_dir_ignore_o_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_ignore_o_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_dir_ignore_o_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    -o    ${ARTIFACT DIR}/ignored_bench_dir_bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    bench ${suite_dir}/bench_case.mla
    Should Not Contain    ${log_text}    -o
    Should Not Contain    ${log_text}    ignored_bench_dir_bin

MLang Frontend Bench Ignores Colon Warning Flags
    [Documentation]    Verify bench mode does not forward -Wno-colon-if/-Wno-colon-while to backend suite invocations.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_nowarn
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_bench_nowarn_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_nowarn_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_nowarn_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    -Wno-colon-if    -Wno-colon-while    ${suite_dir}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    -Wno-colon-if
    Should Not Contain    ${log_text}    -Wno-colon-while

MLang Frontend Bench SingleFile Forwards Colon Warning Flags
    [Documentation]    Verify bench single-file mode preserves -Wno-colon-if/-Wno-colon-while for backend compile.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_warn_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_warn_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_warn_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_bench_warn_single_backend.log
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${bench_file}    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${bench_file}    -Wno-colon-if    -Wno-colon-while
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -Wno-colon-if
    Should Contain    ${log_text}    -Wno-colon-while

MLang Frontend Directory Mode Ignores Output Flag
    [Documentation]    Verify directory suite mode does not forward -o to per-suite backend invocations (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_igno
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_ignore_o_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_ignore_o_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_ignore_o_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}    bench    ${suite_dir}    -o    should_not_forward
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Not Contain    ${log_text}    -o should_not_forward

MLang Frontend Directory Mode Forwards Compact Linker Flags
    [Documentation]    Verify directory suite mode forwards compact -L<dir> and -l<name> linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_compact_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_compact_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_compact_link_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    bench    ${suite_dir}    -L/tmp/mlang_lib    -lmydep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -L/tmp/mlang_lib
    Should Contain    ${log_text}    -lmydep

MLang Frontend Directory Mode Forwards Wl Flags
    [Documentation]    Verify directory suite mode forwards -Wl,<args> linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_wl
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_wl_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_wl_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_dir_wl_backend.log
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${test_code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn dir_wl_flag_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_dir_wl_tests.mla    ${test_code}
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${suite_dir}    -Wl,-rpath,/tmp/mlang_rpath
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -Wl,-rpath,/tmp/mlang_rpath

MLang Frontend Directory Mode Rejects Bare Wl Flag
    [Documentation]    Verify C++ parity: bare -Wl, (without payload) is an unknown option.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_wl_bare
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_wl_bare_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${test_code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn dir_wl_bare_flag_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/test_dir_wl_bare_tests.mla    ${test_code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${suite_dir}    -Wl,
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: -Wl,

MLang Frontend SingleFile Rejects Bare Wl Flag
    [Documentation]    Verify C++ parity: bare -Wl, is rejected in single-file test mode as unknown.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_wl_bare
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_wl_bare.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn single_wl_bare_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${src}    -Wl,
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: -Wl,

MLang Frontend SingleFile Forwards Wl Flags
    [Documentation]    Verify C++ parity: single-file test mode forwards valid -Wl,<args> linker flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_wl
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_wl.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn single_wl_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_wl_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_wl_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${src}    -Wl,-rpath,/tmp/mlang_rpath_single
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -Wl,-rpath,/tmp/mlang_rpath_single

MLang Frontend SingleFile Forwards Compact Linker Flags
    [Documentation]    Verify C++ parity: single-file test mode forwards compact -L<dir> and -l<name> flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_compact_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_compact_link.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn single_compact_link_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_compact_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${src}    -L/tmp/mlang_single_lib    -lsingledep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -L/tmp/mlang_single_lib
    Should Contain    ${log_text}    -lsingledep

MLang Frontend SingleFile Forwards Split Linker Flags
    [Documentation]    Verify C++ parity: single-file test mode forwards split -L <dir> and -l <name> flags.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_split_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_split_link.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn single_split_link_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_split_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_split_link_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${src}    -L    /tmp/mlang_single_split_lib    -l    singlesplitdep
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -L /tmp/mlang_single_split_lib
    Should Contain    ${log_text}    -l singlesplitdep

MLang Frontend SingleFile Forwards OutputFlag
    [Documentation]    Verify C++ parity: single-file test mode forwards `-o <file>`.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_single_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_single_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn single_output_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_output_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_single_output_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    test    ${src}    -o    single_output_bin
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    -o single_output_bin

MLang Frontend Directory Mode Rejects Unknown option
    [Documentation]    Verify test/bench directory mode rejects unknown options instead of silently dropping them.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_dir_unknown
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_dir_unknown_suite
    Run Keyword And Ignore Error    Remove Directory    ${suite_dir}    recursive=True
    Create Directory    ${suite_dir}
    ${bench_code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${suite_dir}/bench_case.mla    ${bench_code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${suite_dir}    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Bench Missing Value Uses Unknown option Error
    [Documentation]    Verify missing value for bench options is reported as unknown option (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_missing
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters
    ${run_warmup}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-warmup
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_warmup.rc}    0
    Should Contain    ${run_warmup.stderr}    Unknown option: --bench-warmup

MLang Frontend Missing LinkOrOutput Value Uses Unknown option Error
    [Documentation]    Verify missing value for -o/-L/-l in test mode reports unknown option and usage (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_missing_link_or_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_missing_link_or_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn missing_link_or_output_value_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}

    ${run_o}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${src}    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_o.rc}    0
    Should Contain    ${run_o.stderr}    Unknown option: -o
    Should Contain    ${run_o.stdout}    Usage:

    ${run_L}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${src}    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_L.rc}    0
    Should Contain    ${run_L.stderr}    Unknown option: -L
    Should Contain    ${run_L.stdout}    Usage:

    ${run_l}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    test    ${src}    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_l.rc}    0
    Should Contain    ${run_l.stderr}    Unknown option: -l
    Should Contain    ${run_l.stdout}    Usage:

MLang Frontend RunTests Missing LinkOrOutput Value Uses Unknown option Error
    [Documentation]    Verify missing value for -o/-L/-l in run tests mode reports unknown option and usage (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_runtests_missing_link_or_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/frontend_runtests_missing_link_or_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    \#[test]
    ...    fn runtests_missing_link_or_output_value_test() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}

    ${run_o}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    run    tests    ${src}    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_o.rc}    0
    Should Contain    ${run_o.stderr}    Unknown option: -o
    Should Contain    ${run_o.stdout}    Usage:

    ${run_L}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    run    tests    ${src}    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_L.rc}    0
    Should Contain    ${run_L.stderr}    Unknown option: -L
    Should Contain    ${run_L.stdout}    Usage:

    ${run_l}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    run    tests    ${src}    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_l.rc}    0
    Should Contain    ${run_l.stderr}    Unknown option: -l
    Should Contain    ${run_l.stdout}    Usage:

MLang Frontend Bench Missing LinkOrOutput Value Uses Unknown option Error
    [Documentation]    Verify missing value for -o/-L/-l in bench mode reports unknown option and usage (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_bench_missing_link_or_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/bench_missing_link_or_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}

    ${run_o}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${src}    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_o.rc}    0
    Should Contain    ${run_o.stderr}    Unknown option: -o
    Should Contain    ${run_o.stdout}    Usage:

    ${run_L}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${src}    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_L.rc}    0
    Should Contain    ${run_L.stderr}    Unknown option: -L
    Should Contain    ${run_L.stdout}    Usage:

    ${run_l}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${src}    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_l.rc}    0
    Should Contain    ${run_l.stderr}    Unknown option: -l
    Should Contain    ${run_l.stdout}    Usage:

MLang Frontend Compile Missing LinkOrOutput Value Uses Unknown option Error
    [Documentation]    Verify C++ parity: missing value for -o/-L/-l in compile mode reports unknown option and usage.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_missing_link_or_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_missing_link_or_output.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}

    ${run_o}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    -o
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_o.rc}    0
    Should Contain    ${run_o.stderr}    Unknown option: -o
    Should Contain    ${run_o.stdout}    Usage:

    ${run_L}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    -L
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_L.rc}    0
    Should Contain    ${run_L.stderr}    Unknown option: -L
    Should Contain    ${run_L.stdout}    Usage:

    ${run_l}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    -l
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_l.rc}    0
    Should Contain    ${run_l.stderr}    Unknown option: -l
    Should Contain    ${run_l.stdout}    Usage:

MLang Frontend Unknown option Prints Usage
    [Documentation]    Verify unknown test/bench options print usage text in addition to error (C++ parity style).
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_unknown_usage
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${EXECDIR}/tests/bench_stdlib.mla    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Compile Mode Rejects Bench Flags
    [Documentation]    Verify C++ parity: non-test compile mode rejects bench-only flags as unknown options.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_benchflag_reject
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_benchflag_reject.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --bench-iters    10
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Compile Mode Rejects Inline Bench Flags
    [Documentation]    Verify C++ parity: non-test compile mode rejects inline bench flag forms.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_inline_benchflag_reject
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_inline_benchflag_reject.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${warmup}=    Set Variable    --bench-warmup=5
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    ${warmup}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-warmup=5
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Compile Mode Rejects NoRun Flag
    [Documentation]    Verify C++ parity: non-test compile mode rejects --no-run as unknown option.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_norun_reject
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_norun_reject.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --no-run
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Compile Mode Surfaces DoubleFree Diagnostics
    [Documentation]    Verify compile-time memory safety diagnostics from backend are surfaced by mlang-frontend-mla.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_double_free_diag
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_double_free_diag.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn free(s: str8) {
    ...        String::free(s);
    ...    }
    ...    fn main() -> i32 {
    ...        let s: str8 = String::from("hello");
    ...        free(s);
    ...        free(s);
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    double free or use-after-free

MLang Frontend Compile Mode Surfaces HandleFree Diagnostics
    [Documentation]    Verify compile-time *_free handle diagnostics are surfaced by mlang-frontend-mla.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_handle_free_diag
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_handle_free_diag.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let a = atomic_i64_new(1);
    ...        atomic_i64_free(a);
    ...        atomic_i64_free(a);
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    double free or use-after-free

MLang Frontend Compile Mode Allows NoRun After Tests
    [Documentation]    Verify C++ left-to-right parity: `--tests` enables later `--no-run` in compile-mode argument stream.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_allow_norun_after_tests
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_allow_norun_after_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_allow_norun_after_tests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_allow_norun_after_tests_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    --no-run
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    --no-run

MLang Frontend Compile Mode Rejects NoRun Before Tests
    [Documentation]    Verify C++ left-to-right parity: `--no-run` before `--tests` remains unknown in compile mode.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_reject_norun_before_tests
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_reject_norun_before_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --no-run    --tests
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --no-run
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Compile Mode Allows BenchFlags After Tests
    [Documentation]    Verify C++ left-to-right parity: `--tests` enables later bench flags in compile-mode stream.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_allow_bench_after_tests
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_allow_bench_after_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_allow_bench_after_tests_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_allow_bench_after_tests_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    --bench-iters    9    --bench-warmup    2
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --tests
    Should Contain    ${log_text}    --bench-iters 9
    Should Contain    ${log_text}    --bench-warmup 2

MLang Frontend Compile Mode Rejects BenchFlags Before Tests
    [Documentation]    Verify C++ left-to-right parity: bench flags before `--tests` are unknown in compile mode.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_reject_bench_before_tests
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_reject_bench_before_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --bench-iters    9    --tests
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Compile Mode TestsFlag Invalid BenchValue Fails Early
    [Documentation]    Verify C++ parity: after --tests in compile stream, invalid bench values fail with explicit diagnostics.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_tests_invalid_bench_value
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_tests_invalid_bench_value.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_invalid_bench_value_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_invalid_bench_value_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    --bench-iters    nope
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Invalid value for --bench-iters
    ${exists}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${exists}    ${False}

MLang Frontend Compile Mode TestsFlag BenchValue Is Normalized
    [Documentation]    Verify C++ parity: after --tests in compile stream, bench values are clamped/normalized before forwarding.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_tests_bench_normalize
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_tests_bench_normalize.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_bench_normalize_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_bench_normalize_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" > "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    --bench-iters    +0000    --bench-warmup    -7
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --bench-iters 1
    Should Contain    ${log_text}    --bench-warmup 0

MLang Frontend Compile Mode TestsFlag Rejects Inline BenchForms
    [Documentation]    Verify C++ parity: after --tests in compile stream, inline bench forms are unknown options.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_tests_inline_bench_reject
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_tests_inline_bench_reject.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_inline_bench_reject_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_inline_bench_reject_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${iters}=    Set Variable    --bench-iters=20
    ${warmup}=    Set Variable    --bench-warmup=5
    ${run}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    ${iters}    ${warmup}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters=20
    ${log_text}=    Get File    ${fake_log}
    Should Contain    ${log_text}    --help

MLang Frontend Compile Mode TestsFlag BenchValue Overflow Fails Early
    [Documentation]    Verify C++ parity: after --tests in compile stream, out-of-range bench values are rejected early.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_tests_bench_overflow
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_tests_bench_overflow.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${fake_backend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_bench_overflow_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/fake_compile_tests_bench_overflow_backend.log
    ${script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "$@" >> "${fake_log}"
    ...    exit 0
    Create File    ${fake_backend}    ${script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x "${fake_backend}"    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${chmod.rc}    0
    ${huge_pos}=    Set Variable    2147483648
    ${run_i}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    --bench-iters    ${huge_pos}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_i.rc}    0
    Should Contain    ${run_i.stderr}    Invalid value for --bench-iters
    ${exists_i}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${exists_i}    ${False}
    ${huge_neg}=    Set Variable    -2147483649
    ${run_w}=    Run Process    ${frontend}    --backend    ${fake_backend}
    ...    ${src}    --tests    --bench-warmup    ${huge_neg}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_w.rc}    0
    Should Contain    ${run_w.stderr}    Invalid value for --bench-warmup
    ${exists_w}=    Run Keyword And Return Status    File Should Exist    ${fake_log}
    Should Be Equal    ${exists_w}    ${False}

MLang Frontend Compile Mode TestsFlag BenchValue Position Treats VersionHelp As Value
    [Documentation]    Verify C++ parity: after --tests in compile stream, --version/--help in bench value slot are invalid values.
    ${frontend}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/mlang_frontend_mla_bin_compile_tests_bench_valuepos
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/compile_tests_bench_valuepos.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run_i}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --tests    --bench-iters    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_i.rc}    0
    Should Contain    ${run_i.stderr}    Invalid value for --bench-iters
    ${run_w}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    ${src}    --tests    --bench-warmup    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run_w.rc}    0
    Should Contain    ${run_w.stderr}    Invalid value for --bench-warmup

Testing Mock Example Runs Correctly
    [Documentation]    Build and run examples/testing_mock_example.mla and verify
    ...                std::testing mock expectations pass.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/testing_mock_example_bin
    ${build}=    Run Process    ${MLANG}    examples/testing_mock_example.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building testing_mock_example.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=testing_mock_example exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    checks=3 failures=0

Argparser Demo Runs Correctly
    [Documentation]    Build and run examples/argparser_demo.mla with various CLI args;
    ...                verify flags, options, and positionals are parsed correctly.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/argparser_demo_bin
    ${build}=    Run Process    ${MLANG}    examples/argparser_demo.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building argparser_demo.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    # Run with --verbose, --output, and a positional
    ${run}=    Run Process    ${bin}    --verbose    --output    result.txt    myfile.txt
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=argparser_demo exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    verbose=1
    Should Contain    ${run.stdout}    dry-run=0
    Should Contain    ${run.stdout}    output=result.txt
    Should Contain    ${run.stdout}    count=1
    Should Contain    ${run.stdout}    positional_count=1
    Should Contain    ${run.stdout}    input=myfile.txt
    Should Contain    ${run.stdout}    (verbose mode active)
    # Run with short flags and --count
    ${run2}=    Run Process    ${bin}    -v    -c    5    -o    out2.txt    file2.txt
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run2.rc}    0
    Should Contain    ${run2.stdout}    verbose=1
    Should Contain    ${run2.stdout}    output=out2.txt
    Should Contain    ${run2.stdout}    count=5
    Should Contain    ${run2.stdout}    input=file2.txt
    # Run with no positional -- shows count=0
    ${run3}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run3.rc}    0
    Should Contain    ${run3.stdout}    verbose=0
    Should Contain    ${run3.stdout}    positional_count=0

Slice Example Runs Correctly
    [Documentation]    Build and run examples/slice.mla; verify key output lines
    ...                for [T;N] type annotation, [val;N] fill literal, enumerate
    ...                loop forms, iter()/into_iter() adaptors, and len().
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/slice_bin
    ${build}=    Run Process    ${MLANG}    examples/slice.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building slice.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=slice example exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    # [T; N] type annotation + len()
    Should Contain    ${run.stdout}    first element:  1
    Should Contain    ${run.stdout}    element count:  5
    # [val; N] fill literal
    Should Contain    ${run.stdout}    [0] = 0
    Should Contain    ${run.stdout}    [0] = 5
    # Dynamic fill count
    Should Contain    ${run.stdout}    [5] = 1
    # sum via iter()
    Should Contain    ${run.stdout}    sum of primes = 41
    # Enumerate loops (all four forms produce identical output)
    Should Contain    ${run.stdout}    0: alpha
    Should Contain    ${run.stdout}    3: delta
    # iter() / into_iter() no-op adaptors
    Should Contain    ${run.stdout}    via .iter():
    Should Contain    ${run.stdout}    via .into_iter():
    Should Contain    ${run.stdout}    100
    # Practical: first index > 90
    Should Contain    ${run.stdout}    first score > 90 at index 0
    # Multiplication table row 7
    Should Contain    ${run.stdout}    7 * 0 = 0
    Should Contain    ${run.stdout}    7 * 9 = 63

Lambda Fold Patterns Demo Runs Correctly
    [Documentation]    Build and run examples/lambda_fold_patterns.mla and
    ...                verify typed lambda and fold-expression outputs.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/lambda_fold_patterns_bin
    ${build}=    Run Process    ${MLANG}    examples/lambda_fold_patterns.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building lambda_fold_patterns.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=lambda_fold_patterns exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    accumulator=10
    Should Contain    ${run.stdout}    sum=10 product=24
    Should Contain    ${run.stdout}    all_true=0 any_true=1
    Should Contain    ${run.stdout}    empty_sum=0 empty_prod=1
    Should Contain    ${run.stdout}    empty_all=1 empty_any=0

Lambda Fold Advanced Demo Runs Correctly
    [Documentation]    Build and run examples/lambda_fold_advanced.mla and
    ...                verify nested lambda generation and fold reductions.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/lambda_fold_advanced_bin
    ${build}=    Run Process    ${MLANG}    examples/lambda_fold_advanced.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building lambda_fold_advanced.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=lambda_fold_advanced exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    weight_sum=19
    Should Contain    ${run.stdout}    weight_product=972
    Should Contain    ${run.stdout}    all_even=0 any_big=1
    Should Contain    ${run.stdout}    empty_sum=0 empty_mul=1 empty_all=1 empty_any=0

Printf And GetChar Demo
    [Documentation]    Verify std::printf (printf/eprintf/fprintf) compile and
    ...                produce correct output; get_char is verified via compile only.
    ${src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/printf_demo.mla
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/printf_demo_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    mod std::printf;
    ...    mod std::io;
    ...    use std::printf::printf;
    ...    use std::printf::eprintf;
    ...    use std::printf::fprintf;
    ...    use std::io::get_char;
    ...    fn main() -> i32 {
    ...        printf(format!("hello printf\\n"));
    ...        printf(format!("val={}\\n", 7));
    ...        fprintf(1, format!("fprintf stdout\\n"));
    ...        eprintf(format!("eprintf stderr\\n"));
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building printf_demo (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=printf_demo exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    hello printf
    Should Contain    ${run.stdout}    val=7
    Should Contain    ${run.stdout}    fprintf stdout
    Should Contain    ${run.stderr}    eprintf stderr

Fs Lines Demo Runs Correctly
    [Documentation]    Build and run examples/std_fs_lines.mla; verify that
    ...                BufReader filters comment lines, counts data rows,
    ...                prints each data line, and finds entries by content.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_fs_lines_bin
    ${build}=    Run Process    ${MLANG}    examples/std_fs_lines.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building std_fs_lines.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=std_fs_lines exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    header: name,score
    Should Contain    ${run.stdout}    data: Alice,95
    Should Contain    ${run.stdout}    data: Bob,87
    Should Contain    ${run.stdout}    data: Carol,92
    Should Contain    ${run.stdout}    data: Dave,78
    Should Contain    ${run.stdout}    total_lines=7
    Should Contain    ${run.stdout}    comments=2
    Should Contain    ${run.stdout}    data=4
    Should Contain    ${run.stdout}    found: Alice,95

Fs Seek Demo Runs Correctly
    [Documentation]    Build and run examples/std_fs_seek.mla; verify
    ...                SeekFrom::start/current/end, File::tell, and
    ...                File::size all produce the expected output.
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_fs_seek_bin
    ${build}=    Run Process    ${MLANG}    examples/std_fs_seek.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building std_fs_seek.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=std_fs_seek exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    size=20
    Should Contain    ${run.stdout}    start(0)+4: 0123
    Should Contain    ${run.stdout}    start(10)+4: ABCD
    Should Contain    ${run.stdout}    end(-4)+4: GHIJ
    Should Contain    ${run.stdout}    tell=20
    Should Contain    ${run.stdout}    start(4),skip(2),+4: 6789
    Should Contain    ${run.stdout}    first3: 012

Fs Rw Demo Runs Correctly
    [Documentation]    Build and run examples/std_fs_rw.mla; verify
    ...                File::create, File::append, and File::open_rw
    ...                (in-place field patching with seek+write_bytes).
    ${bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_fs_rw_bin
    ${build}=    Run Process    ${MLANG}    examples/std_fs_rw.mla    -o    ${bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building std_fs_rw.mla (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=std_fs_rw exited with rc=${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    after create
    Should Contain    ${run.stdout}    STATUS:PENDING
    Should Contain    ${run.stdout}    after append
    Should Contain    ${run.stdout}    LOG:entry1
    Should Contain    ${run.stdout}    LOG:entry2
    Should Contain    ${run.stdout}    after patch
    Should Contain    ${run.stdout}    STATUS:DONE
    Should Contain    ${run.stdout}    final_size=45

Multithreaded Net Server Client Roundtrip
    [Documentation]    Build and run multithreaded std::net server/client examples
    ...                and verify concurrent echo roundtrips succeed.
    ${server_bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_net_mt_server_bin
    ${client_bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_net_mt_client_bin
    ${server_out}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_net_mt_server.out
    ${server_err}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/std_net_mt_server.err
    ${PORT}=    Set Variable    18788

    ${build_server}=    Run Process    ${MLANG}    examples/std_net_mt_server.mla    -o    ${server_bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_server.rc}    0
    ...    msg=Failed building std_net_mt_server (rc=${build_server.rc})\nSTDOUT:\n${build_server.stdout}\nSTDERR:\n${build_server.stderr}

    ${build_client}=    Run Process    ${MLANG}    examples/std_net_mt_client.mla    -o    ${client_bin}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_client.rc}    0
    ...    msg=Failed building std_net_mt_client (rc=${build_client.rc})\nSTDOUT:\n${build_client.stdout}\nSTDERR:\n${build_client.stderr}

    Start Process    ${server_bin}    --port    ${PORT}
    ...    alias=net_server    stdout=${server_out}    stderr=${server_err}
    Sleep    1s
    ${server_out_text}=    Get File    ${server_out}
    ${server_err_text}=    Get File    ${server_err}
    ${bind_denied_out}=    Evaluate    "Operation not permitted" in """${server_out_text}"""
    ${bind_denied_err}=    Evaluate    "Operation not permitted" in """${server_err_text}"""
    ${bind_denied}=    Evaluate    ${bind_denied_out} or ${bind_denied_err}
    Pass Execution If    ${bind_denied}    Skipping net roundtrip: socket bind is not permitted in this environment.

    ${client_run}=    Run Process    ${client_bin}    --port    ${PORT}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${client_run.rc}    0
    ...    msg=std_net_mt_client failed (rc=${client_run.rc})\nSTDOUT:\n${client_run.stdout}\nSTDERR:\n${client_run.stderr}
    Should Contain    ${client_run.stdout}    CLIENT_DONE ok=2

    ${server_res}=    Wait For Process    net_server    timeout=10s
    Should Be Equal As Integers    ${server_res.rc}    0
    ...    msg=std_net_mt_server failed (rc=${server_res.rc})\nSTDOUT:\n${server_res.stdout}\nSTDERR:\n${server_res.stderr}

    ${server_stdout}=    Get File    ${server_out}
    Should Contain    ${server_stdout}    SERVER_READY
    Should Contain    ${server_stdout}    SERVER_CLIENT_OK id=1
    Should Contain    ${server_stdout}    SERVER_CLIENT_OK id=2
    Should Contain    ${server_stdout}    SERVER_DONE handled=2

Tls Client Server Handshake With Generated Certificate
    [Documentation]    Generate a self-signed certificate with openssl, build
    ...                temporary std::ssl server/client programs, and verify a
    ...                localhost TLS handshake plus ping/pong exchange.
    ${openssl_check}=    Run Process    openssl    version
    Pass Execution If    ${openssl_check.rc} != 0    Skipping TLS robot test: openssl command is not available.

    ${server_src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_robot_server.mla
    ${client_src}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_robot_client.mla
    ${server_bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_robot_server_bin
    ${client_bin}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_robot_client_bin
    ${server_out}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_robot_server.out
    ${server_err}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_robot_server.err
    ${cert}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_cert.pem
    ${key}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/tls_key.pem
    ${PORT}=    Set Variable    18889

    ${cert_cmd}=    Catenate    SEPARATOR=\n
    ...    set -e
    ...    rm -f '${cert}' '${key}'
    ...    openssl req -x509 -newkey rsa:2048 -keyout '${key}' -out '${cert}' -days 30 -nodes -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost'
    ${cert_r}=    Run Process    /bin/sh    -lc    ${cert_cmd}    cwd=${ARTIFACT DIR}
    Should Be Equal As Integers    ${cert_r.rc}    0
    ...    msg=Failed generating TLS certificate (rc=${cert_r.rc})\nSTDOUT:\n${cert_r.stdout}\nSTDERR:\n${cert_r.stderr}
    File Should Exist    ${cert}
    File Should Exist    ${key}

    ${server_code}=    Catenate    SEPARATOR=\n
    ...    mod std::io;
    ...    mod std::ssl;
    ...    mod std::strbuf;
    ...    use std::ssl::TlsListener;
    ...    use std::ssl::TlsStream;
    ...    use std::ssl::last_error;
    ...    use std::strbuf::eq;
    ...    fn main() -> i32 {
    ...        let listener_r: result<TlsListener, str8> = TlsListener::bind("127.0.0.1", ${PORT}, "tls_cert.pem", "tls_key.pem");
    ...        if listener_r.is_err() {
    ...            eprintln!("TLS_SERVER_BIND_ERR {}", last_error());
    ...            return 2;
    ...        }
    ...        let listener: TlsListener = listener_r.unwrap();
    ...        println!("TLS_SERVER_READY");
    ...        let stream_r: result<TlsStream, str8> = listener.accept();
    ...        if stream_r.is_err() {
    ...            eprintln!("TLS_SERVER_ACCEPT_ERR {}", last_error());
    ...            listener.close();
    ...            return 3;
    ...        }
    ...        let stream: TlsStream = stream_r.unwrap();
    ...        var buf = String::with_capacity(64);
    ...        let read_r: result<i64, str8> = stream.read(buf, 64);
    ...        if read_r.is_err() {
    ...            eprintln!("TLS_SERVER_READ_ERR {}", last_error());
    ...            String::free(buf);
    ...            stream.close();
    ...            listener.close();
    ...            return 4;
    ...        }
    ...        if eq(buf, "ping") != 1 {
    ...            eprintln!("TLS_SERVER_BAD_PAYLOAD {}", buf);
    ...            String::free(buf);
    ...            stream.close();
    ...            listener.close();
    ...            return 5;
    ...        }
    ...        String::free(buf);
    ...        let write_r: result<i64, str8> = stream.write("pong");
    ...        if write_r.is_err() {
    ...            eprintln!("TLS_SERVER_WRITE_ERR {}", last_error());
    ...            stream.close();
    ...            listener.close();
    ...            return 6;
    ...        }
    ...        println!("TLS_SERVER_OK");
    ...        stream.close();
    ...        listener.close();
    ...        return 0;
    ...    }
    Create File    ${server_src}    ${server_code}

    ${client_code}=    Catenate    SEPARATOR=\n
    ...    mod std::io;
    ...    mod std::ssl;
    ...    mod std::strbuf;
    ...    use std::ssl::TlsStream;
    ...    use std::ssl::last_error;
    ...    use std::strbuf::eq;
    ...    fn main() -> i32 {
    ...        let client_r: result<TlsStream, str8> = TlsStream::connect_with_options("127.0.0.1", ${PORT}, "localhost", "tls_cert.pem", 1);
    ...        if client_r.is_err() {
    ...            eprintln!("TLS_CLIENT_CONNECT_ERR {}", last_error());
    ...            return 2;
    ...        }
    ...        let client: TlsStream = client_r.unwrap();
    ...        let write_r: result<i64, str8> = client.write("ping");
    ...        if write_r.is_err() {
    ...            eprintln!("TLS_CLIENT_WRITE_ERR {}", last_error());
    ...            client.close();
    ...            return 3;
    ...        }
    ...        var buf = String::with_capacity(64);
    ...        let read_r: result<i64, str8> = client.read(buf, 64);
    ...        if read_r.is_err() {
    ...            eprintln!("TLS_CLIENT_READ_ERR {}", last_error());
    ...            String::free(buf);
    ...            client.close();
    ...            return 4;
    ...        }
    ...        if eq(buf, "pong") != 1 {
    ...            eprintln!("TLS_CLIENT_BAD_PAYLOAD {}", buf);
    ...            String::free(buf);
    ...            client.close();
    ...            return 5;
    ...        }
    ...        String::free(buf);
    ...        println!("TLS_CLIENT_OK");
    ...        client.close();
    ...        return 0;
    ...    }
    Create File    ${client_src}    ${client_code}

    ${build_server}=    Run Process    ${MLANG}    ${server_src}    -o    ${server_bin}
    ...    cwd=${ARTIFACT DIR}
    Should Be Equal As Integers    ${build_server.rc}    0
    ...    msg=Failed building TLS server (rc=${build_server.rc})\nSTDOUT:\n${build_server.stdout}\nSTDERR:\n${build_server.stderr}

    ${build_client}=    Run Process    ${MLANG}    ${client_src}    -o    ${client_bin}
    ...    cwd=${ARTIFACT DIR}
    Should Be Equal As Integers    ${build_client.rc}    0
    ...    msg=Failed building TLS client (rc=${build_client.rc})\nSTDOUT:\n${build_client.stdout}\nSTDERR:\n${build_client.stderr}

    Start Process    ${server_bin}
    ...    alias=tls_server    stdout=${server_out}    stderr=${server_err}    cwd=${ARTIFACT DIR}
    Sleep    1s
    ${server_out_text}=    Get File    ${server_out}
    ${server_err_text}=    Get File    ${server_err}
    ${bind_denied_out}=    Evaluate    "Operation not permitted" in """${server_out_text}"""
    ${bind_denied_err}=    Evaluate    "Operation not permitted" in """${server_err_text}"""
    ${bind_denied}=    Evaluate    ${bind_denied_out} or ${bind_denied_err}
    Pass Execution If    ${bind_denied}    Skipping TLS robot test: socket bind is not permitted in this environment.

    ${client_run}=    Run Process    ${client_bin}    cwd=${ARTIFACT DIR}
    Should Be Equal As Integers    ${client_run.rc}    0
    ...    msg=TLS client failed (rc=${client_run.rc})\nSTDOUT:\n${client_run.stdout}\nSTDERR:\n${client_run.stderr}
    Should Contain    ${client_run.stdout}    TLS_CLIENT_OK

    ${server_res}=    Wait For Process    tls_server    timeout=10s
    Should Be Equal As Integers    ${server_res.rc}    0
    ...    msg=TLS server failed (rc=${server_res.rc})\nSTDOUT:\n${server_res.stdout}\nSTDERR:\n${server_res.stderr}

    ${server_stdout}=    Get File    ${server_out}
    Should Contain    ${server_stdout}    TLS_SERVER_READY
    Should Contain    ${server_stdout}    TLS_SERVER_OK

Pkg Fetch Build Parity (CPP vs MLA)
    [Documentation]    Create a local git C dependency and verify both
    ...                package-manager backends (MLANG_PKG_IMPL=cpp|mla)
    ...                pass init/add/fetch/build and produce runnable binaries.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_parity
    ${dep_repo}=    Catenate    SEPARATOR=    ${base}/depmini
    ${cpp_proj}=    Catenate    SEPARATOR=    ${base}/pkg_cpp_app
    ${mla_proj}=    Catenate    SEPARATOR=    ${base}/pkg_mla_app

    ${setup}=    Catenate    SEPARATOR=\n
    ...    set -e
    ...    rm -rf '${base}'
    ...    mkdir -p '${dep_repo}' '${cpp_proj}/src' '${mla_proj}/src'
    ...    cat > '${dep_repo}/CMakeLists.txt' <<'EOF'
    ...    cmake_minimum_required(VERSION 3.10)
    ...    project(depmini C)
    ...    add_library(depmini STATIC dep.c)
    ...    EOF
    ...    cat > '${dep_repo}/dep.c' <<'EOF'
    ...    int depmini_add(int a, int b) { return a + b; }
    ...    EOF
    ...    cp '${EXECDIR}/examples/pkg_workflow_main.mla' '${cpp_proj}/src/main.mla'
    ...    cp '${EXECDIR}/examples/pkg_workflow_main.mla' '${mla_proj}/src/main.mla'
    ...    cd '${dep_repo}'
    ...    git init -q
    ...    git add .
    ...    git -c user.name=robot -c user.email=robot@example.com commit -q -m init
    ${setup_r}=    Run Process    /bin/sh    -lc    ${setup}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${setup_r.rc}    0
    ...    msg=Package fixture setup failed (rc=${setup_r.rc})\nSTDOUT:\n${setup_r.stdout}\nSTDERR:\n${setup_r.stderr}

    ${cpp_init}=    Run Process    ${MLANG}    pkg    init
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_init.rc}    0
    ${cpp_add}=    Run Process    ${MLANG}    pkg    add    depmini    --git    ${dep_repo}
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_add.rc}    0
    ${cpp_fetch}=    Run Process    ${MLANG}    pkg    fetch
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_fetch.rc}    0
    ${cpp_build}=    Run Process    ${MLANG}    pkg    build    -O0
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_build.rc}    0
    File Should Exist    ${cpp_proj}/build/pkg_cpp_app
    ${cpp_run}=    Run Process    ${cpp_proj}/build/pkg_cpp_app    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_run.rc}    0
    Should Contain    ${cpp_run.stdout}    pkg workflow ok

    ${mla_init}=    Run Process    ${MLANG}    pkg    init
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_init.rc}    0
    ${mla_add}=    Run Process    ${MLANG}    pkg    add    depmini    --git    ${dep_repo}
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_add.rc}    0
    ${mla_fetch}=    Run Process    ${MLANG}    pkg    fetch
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_fetch.rc}    0
    ${mla_build}=    Run Process    ${MLANG}    pkg    build    -O0
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_build.rc}    0
    File Should Exist    ${mla_proj}/build/pkg_mla_app
    ${mla_run}=    Run Process    ${mla_proj}/build/pkg_mla_app    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_run.rc}    0
    Should Contain    ${mla_run.stdout}    pkg workflow ok

Pkg PkgConfig Parity (CPP vs MLA)
    [Documentation]    Verify pkg-config based c-dependencies behave identically
    ...                for both package-manager backends using a local fake
    ...                pkg-config executable.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_cfg_parity
    ${fakebin}=    Catenate    SEPARATOR=    ${base}/fakebin
    ${cpp_proj}=    Catenate    SEPARATOR=    ${base}/pkgcfg_cpp_app
    ${mla_proj}=    Catenate    SEPARATOR=    ${base}/pkgcfg_mla_app
    ${path_env}=    Catenate    SEPARATOR=    ${fakebin}:%{PATH}

    ${setup}=    Catenate    SEPARATOR=\n
    ...    set -e
    ...    rm -rf '${base}'
    ...    mkdir -p '${fakebin}' '${cpp_proj}/src' '${mla_proj}/src'
    ...    cp '${EXECDIR}/examples/pkg_workflow_main.mla' '${cpp_proj}/src/main.mla'
    ...    cp '${EXECDIR}/examples/pkg_workflow_main.mla' '${mla_proj}/src/main.mla'
    ...    cat > '${fakebin}/pkg-config' <<'EOF'
    ...    \#!/bin/sh
    ...    if [ "$1" = "--cflags" ] && [ "$2" = "--libs" ]; then
    ...      echo "-lm"
    ...      exit 0
    ...    fi
    ...    exit 1
    ...    EOF
    ...    chmod +x '${fakebin}/pkg-config'
    ${setup_r}=    Run Process    /bin/sh    -lc    ${setup}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${setup_r.rc}    0
    ...    msg=pkg-config fixture setup failed (rc=${setup_r.rc})\nSTDOUT:\n${setup_r.stdout}\nSTDERR:\n${setup_r.stderr}

    ${cpp_init}=    Run Process    ${MLANG}    pkg    init
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_init.rc}    0
    ${cpp_add}=    Run Process    ${MLANG}    pkg    add    fakelib    --pkg-config    fakelib
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_add.rc}    0
    ${cpp_fetch}=    Run Process    ${MLANG}    pkg    fetch
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_fetch.rc}    0
    ${cpp_build}=    Run Process    ${MLANG}    pkg    build    -O0
    ...    cwd=${cpp_proj}    env:MLANG_PKG_IMPL=cpp    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_build.rc}    0
    File Should Exist    ${cpp_proj}/build/pkgcfg_cpp_app
    ${cpp_run}=    Run Process    ${cpp_proj}/build/pkgcfg_cpp_app    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${cpp_run.rc}    0
    Should Contain    ${cpp_run.stdout}    pkg workflow ok

    ${mla_init}=    Run Process    ${MLANG}    pkg    init
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_init.rc}    0
    ${mla_add}=    Run Process    ${MLANG}    pkg    add    fakelib    --pkg-config    fakelib
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_add.rc}    0
    ${mla_fetch}=    Run Process    ${MLANG}    pkg    fetch
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_fetch.rc}    0
    ${mla_build}=    Run Process    ${MLANG}    pkg    build    -O0
    ...    cwd=${mla_proj}    env:MLANG_PKG_IMPL=mla    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_build.rc}    0
    File Should Exist    ${mla_proj}/build/pkgcfg_mla_app
    ${mla_run}=    Run Process    ${mla_proj}/build/pkgcfg_mla_app    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${mla_run.rc}    0
    Should Contain    ${mla_run.stdout}    pkg workflow ok

Pkg Dependency Toolchains Report Found Missing And Old Versions
    [Documentation]    Verify dependency toolchain preflight output, minimum
    ...                versions, install hints, and MLA-to-C++ delegation.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_toolchains
    ${fakebin}=    Catenate    SEPARATOR=    ${base}/fakebin
    ${manifest}=    Catenate    SEPARATOR=    ${base}/mlang.toml
    ${path_env}=    Catenate    SEPARATOR=    ${fakebin}:%{PATH}
    Create Directory    ${fakebin}

    ${tool_script}=    Catenate    SEPARATOR=\n
    ...    \#!/bin/sh
    ...    echo "fixture-tool version 2.4.1"
    Create File    ${fakebin}/fixture-tool    ${tool_script}
    ${chmod}=    Run Process    /bin/sh    -lc    chmod +x '${fakebin}/fixture-tool'
    Should Be Equal As Integers    ${chmod.rc}    0

    ${passing_manifest}=    Catenate    SEPARATOR=\n
    ...    [package]
    ...    name = "toolchain_fixture"
    ...    version = "0.1.0"
    ...    [dependencies]
    ...    [tool.mlang.toolchains]
    ...    fixture = { name = "Fixture Tool", command = "fixture-tool", min_version = "2.0", install = "install fixture-tool" }
    Create File    ${manifest}    ${passing_manifest}

    ${found}=    Run Process    ${MLANG}    pkg    fetch
    ...    cwd=${base}    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${found.rc}    0
    ...    msg=Toolchain success case failed (rc=${found.rc})\nSTDOUT:\n${found.stdout}\nSTDERR:\n${found.stderr}
    Should Contain    ${found.stdout}    -- Checking dependency toolchains for
    Should Contain    ${found.stdout}    -- Found Fixture Tool:
    Should Contain    ${found.stdout}    (version 2.4.1, requires >= 2.0)
    Should Contain    ${found.stdout}    -- Dependency toolchain check completed

    ${failing_manifest}=    Catenate    SEPARATOR=\n
    ...    [package]
    ...    name = "toolchain_fixture"
    ...    version = "0.1.0"
    ...    [dependencies]
    ...    [tool.mlang.toolchains]
    ...    old = { name = "Old Fixture Tool", command = "fixture-tool", min_version = "9.0", install = "upgrade fixture-tool" }
    ...    missing = { name = "Missing Fixture Tool", command = "fixture-tool-missing", install = "install fixture-tool-missing" }
    Create File    ${manifest}    ${failing_manifest}

    ${failed}=    Run Process    ${MLANG}    pkg    fetch
    ...    cwd=${base}    env:MLANG_PKG_IMPL=mla    env:PATH=${path_env}    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${failed.rc}    0
    Should Contain    ${failed.stderr}    -- Found Old Fixture Tool:
    Should Contain    ${failed.stderr}    requires >= 9.0
    Should Contain    ${failed.stderr}    Install or upgrade: upgrade fixture-tool
    Should Contain    ${failed.stderr}    -- Missing Missing Fixture Tool:
    Should Contain    ${failed.stderr}    Install: install fixture-tool-missing
    Should Contain    ${failed.stderr}    Required dependency toolchains are missing or too old.

Pkg Builds Explicitly Included Packages Into Isolated Targets
    [Documentation]    Verify root [[include]] selection, directory and file
    ...                paths, MLA frontend delegation, and isolated outputs.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_includes
    ${setup}=    Run Process    /bin/sh    -lc
    ...    rm -rf '${base}' && cp -R '${EXECDIR}/examples/package_manager_includes' '${base}'
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${setup.rc}    0

    ${build}=    Run Process    ${MLANG}    pkg    build
    ...    cwd=${base}    env:MLANG_PKG_IMPL=mla    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Included-package build failed (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    Should Not Contain    ${build.stdout}    Build failed.
    File Should Exist    ${base}/build/editor/editor
    File Should Exist    ${base}/build/editor/asset-compiler
    File Should Exist    ${base}/build/converter/converter
    File Should Not Exist    ${base}/apps/editor/build/editor
    File Should Not Exist    ${base}/tools/converter/build/converter

    ${editor}=    Run Process    ${base}/build/editor/editor    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${editor.rc}    0
    Should Contain    ${editor.stdout}    editor package built in its include target
    ${converter}=    Run Process    ${base}/build/converter/converter    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${converter.rc}    0
    Should Contain    ${converter.stdout}    converter package built separately

Pkg Lock Pins Git And Verifies Archive Checksums Offline
    [Documentation]    Verify exact Git revisions, archive SHA-256 locking,
    ...                locked manifest checks, offline cache use, and tamper detection.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_reproducibility
    ${origin}=    Catenate    SEPARATOR=    ${base}/git-origin
    ${project}=    Catenate    SEPARATOR=    ${base}/project
    ${archive}=    Catenate    SEPARATOR=    ${base}/archive_dep.tar.gz
    ${setup}=    Catenate    SEPARATOR=\n
    ...    set -e
    ...    rm -rf '${base}'
    ...    mkdir -p '${origin}' '${project}/src' '${base}/archive-root/archive_dep'
    ...    printf 'locked git fixture\n' > '${origin}/README.txt'
    ...    git -C '${origin}' init -q
    ...    git -C '${origin}' config user.email fixture@example.invalid
    ...    git -C '${origin}' config user.name fixture
    ...    git -C '${origin}' add README.txt
    ...    git -C '${origin}' commit -qm initial
    ...    printf 'locked archive fixture\n' > '${base}/archive-root/archive_dep/data.txt'
    ...    tar -czf '${archive}' -C '${base}/archive-root' archive_dep
    ...    printf 'fn main() { println!("reproducible package"); }\n' > '${project}/src/main.mla'
    ...    cat > '${project}/mlang.toml' <<EOF
    ...    [package]
    ...    name = "reproducible_package"
    ...    version = "0.1.0"
    ...    entry = "src/main.mla"
    ...    [dependencies]
    ...    git_dep = { git = "${origin}", build = "none", spinner = false }
    ...    archive_dep = { url = "file://${archive}", archive = "tar.gz", strip_components = "1", build = "none", spinner = false }
    ...    EOF
    ${setup_result}=    Run Process    /bin/sh    -lc    ${setup}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${setup_result.rc}    0
    ...    msg=Reproducibility fixture setup failed\n${setup_result.stdout}\n${setup_result.stderr}

    ${lock}=    Run Process    ${MLANG}    pkg    lock
    ...    cwd=${project}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${lock.rc}    0
    ...    msg=pkg lock failed\n${lock.stdout}\n${lock.stderr}
    File Should Exist    ${project}/mlang.lock
    ${lock_text}=    Get File    ${project}/mlang.lock
    Should Contain    ${lock_text}    source = "git"
    Should Contain    ${lock_text}    revision = "
    Should Contain    ${lock_text}    source = "archive"
    Should Contain    ${lock_text}    checksum = "sha256:

    ${verify}=    Run Process    ${MLANG}    pkg    verify
    ...    cwd=${project}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${verify.rc}    0
    Should Contain    ${verify.stdout}    mlang.lock and fetched dependencies verified.

    ${build}=    Run Process    ${MLANG}    pkg    build    --locked
    ...    cwd=${project}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    File Should Exist    ${project}/build/reproducible_package

    ${remove_origins}=    Run Process    /bin/sh    -lc
    ...    rm -rf '${origin}' '${archive}'
    Should Be Equal As Integers    ${remove_origins.rc}    0
    ${offline}=    Run Process    ${MLANG}    pkg    fetch    --offline
    ...    cwd=${project}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${offline.rc}    0
    ...    msg=Offline fetch failed\n${offline.stdout}\n${offline.stderr}

    ${stale}=    Run Process    /bin/sh    -lc
    ...    cp '${project}/mlang.toml' '${project}/mlang.toml.saved' && sed 's/archive = "tar.gz"/archive = "zip"/' '${project}/mlang.toml.saved' > '${project}/mlang.toml'
    Should Be Equal As Integers    ${stale.rc}    0
    ${locked_stale}=    Run Process    ${MLANG}    pkg    fetch    --locked
    ...    cwd=${project}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${locked_stale.rc}    0
    Should Contain    ${locked_stale.stderr}    mlang.lock is out of date
    ${restore}=    Run Process    /bin/sh    -lc
    ...    mv '${project}/mlang.toml.saved' '${project}/mlang.toml'
    Should Be Equal As Integers    ${restore.rc}    0

    ${tamper}=    Run Process    /bin/sh    -lc
    ...    printf tampered >> '${project}/build/deps/.archives/archive_dep.tar.gz'
    Should Be Equal As Integers    ${tamper.rc}    0
    ${verify_tampered}=    Run Process    ${MLANG}    pkg    verify
    ...    cwd=${project}    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${verify_tampered.rc}    0
    Should Contain    ${verify_tampered.stderr}    Archive verification failed

Pkg Builds And Links MLang Dynamic Library Target
    [Documentation]    Verify [[lib]] output, depends_on build ordering,
    ...                automatic linking, and loader-relative execution.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_dynamic_library
    ${setup}=    Run Process    /bin/sh    -lc
    ...    rm -rf '${base}' && cp -R '${EXECDIR}/examples/package_manager_dynamic_library' '${base}'
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${setup.rc}    0
    ...    msg=Dynamic-library fixture setup failed\n${setup.stdout}\n${setup.stderr}

    ${build}=    Run Process    ${MLANG}    pkg    build
    ...    cwd=${base}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Dynamic-library package build failed (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    Should Contain    ${build.stdout}    Building dynamic library target 'arithmetic'
    Should Contain    ${build.stdout}    Compiling target 'dynamic_library_demo'
    ${library_index}=    Evaluate    """${build.stdout}""".find("Building dynamic library target 'arithmetic'")
    ${binary_index}=    Evaluate    """${build.stdout}""".find("Compiling target 'dynamic_library_demo'")
    Should Be True    ${library_index} >= 0 and ${library_index} < ${binary_index}

    ${suffix}=    Evaluate    '.dylib' if __import__('platform').system() == 'Darwin' else ('.dll' if __import__('platform').system() == 'Windows' else '.so')
    ${prefix}=    Evaluate    '' if __import__('platform').system() == 'Windows' else 'lib'
    File Should Exist    ${base}/build/${prefix}arithmetic${suffix}
    File Should Exist    ${base}/build/dynamic_library_demo

    ${run}=    Run Process    ${base}/build/dynamic_library_demo
    ...    cwd=${base}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=Dynamic-library demo failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    dynamic library results: sum=42, product=42

Pkg Builds And Links MLang Static Library Target
    [Documentation]    Verify [[lib]] type=static archive output, depends_on
    ...                ordering, direct archive linking, and execution.
    ${base}=    Catenate    SEPARATOR=    ${ARTIFACT DIR}/pkg_static_library
    ${setup}=    Run Process    /bin/sh    -lc
    ...    rm -rf '${base}' && cp -R '${EXECDIR}/examples/package_manager_static_library' '${base}'
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${setup.rc}    0
    ...    msg=Static-library fixture setup failed\n${setup.stdout}\n${setup.stderr}

    ${build}=    Run Process    ${MLANG}    pkg    build
    ...    cwd=${base}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Static-library package build failed (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    Should Contain    ${build.stdout}    Building static library target 'arithmetic_static'
    Should Contain    ${build.stdout}    Compiling target 'static_library_demo'
    ${library_index}=    Evaluate    """${build.stdout}""".find("Building static library target 'arithmetic_static'")
    ${binary_index}=    Evaluate    """${build.stdout}""".find("Compiling target 'static_library_demo'")
    Should Be True    ${library_index} >= 0 and ${library_index} < ${binary_index}

    File Should Exist    ${base}/build/libarithmetic_static.a
    File Should Exist    ${base}/build/static_library_demo
    ${archive}=    Run Process    ar    -t    ${base}/build/libarithmetic_static.a
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${archive.rc}    0
    Should Not Be Empty    ${archive.stdout}

    ${run}=    Run Process    ${base}/build/static_library_demo
    ...    cwd=${base}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=Static-library demo failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    static library results: difference=42, square=49

    ${dynamic_check}=    Run Process    /bin/sh    -lc
    ...    if command -v otool >/dev/null 2>&1; then ! otool -L '${base}/build/static_library_demo' | grep -q arithmetic_static; elif command -v ldd >/dev/null 2>&1; then ! ldd '${base}/build/static_library_demo' | grep -q arithmetic_static; fi
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${dynamic_check.rc}    0
    ...    msg=Executable unexpectedly depends dynamically on arithmetic_static\n${dynamic_check.stdout}\n${dynamic_check.stderr}

*** Keywords ***
Initialize Artifact Dir
    ${artifact_dir}=    Set Variable    ${OUTPUT DIR}
    Create Directory    ${artifact_dir}
    Set Suite Variable    ${ARTIFACT DIR}    ${artifact_dir}
