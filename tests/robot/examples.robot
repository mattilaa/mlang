*** Settings ***
Library           Process
Library           OperatingSystem

*** Variables ***
${MLANG}           ./build/mlang
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
    ${tmp}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/conflicting_types.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct Foo { var x: i32; };
    ...    enum Foo { A };
    Create File    ${tmp}    ${code}
    ${result}=    Run Process    ${MLANG}    -c    ${tmp}    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${result.rc}    0
    Should Contain    ${result.stderr}    type name 'Foo' conflicts with earlier struct defined at line 1

Compile Errors For Reserved Type Keywords
    ${tmp}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/reserved_keyword.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    struct list { var x: i32; };
    Create File    ${tmp}    ${code}
    ${result}=    Run Process    ${MLANG}    -c    ${tmp}    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${result.rc}    0
    Should Contain    ${result.stderr}    expected identifier, found keyword 'list'

Main Accepts Command Line Arguments
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/main_args.mla
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/main_args_bin
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
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/main_ternary_return.mla
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/main_ternary_return_bin
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
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/main_default_return.mla
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/main_default_return_bin
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
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/compile_error_nonzero.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let x: i32 = ;
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${OUTPUT DIR}/compile_error_bin    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${build.rc}    0    msg=Expected nonzero exit for compile error, got ${build.rc}\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}

Result Methods And Unwrap Warns
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/result_unwrap_warn.mla
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/result_unwrap_warn_bin
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        let r: Result<i32, string> = Ok<i32, string>(42);
    ...        if r.is_ok(): {
    ...            println!("{}", r.unwrap());
    ...        } else: {
    ...            println!("err");
    ...        }
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${build}=    Run Process    ${MLANG}    ${src}    -o    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0    msg=Failed building Result unwrap test (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    Should Contain    ${build.stderr}    Result.unwrap() may panic
    ${run}=    Run Process    ${bin}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    42

Mlang Test Runner
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/test_runner.mla
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
    ...    stdout=PIPE    stderr=PIPE    cwd=${OUTPUT DIR}    env:PATH=${OUTPUT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    1    msg=Expected 1 failing test, got ${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Mlang Test Sample Directory
    ${run}=    Run Process    ${MLANG}    test    ${EXECDIR}/tests
    ...    stdout=PIPE    stderr=PIPE    cwd=${OUTPUT DIR}    env:PATH=${OUTPUT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0    msg=Expected sample tests to pass, got ${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Mlang Bench Runner
    [Documentation]    Run stdlib benchmark suite with bench mode and verify benchmark output.
    ${run}=    Run Process    ${MLANG}    bench    ${EXECDIR}/tests/bench_stdlib.mla    --bench-iters    200    --bench-warmup    50
    ...    stdout=PIPE    stderr=PIPE    cwd=${OUTPUT DIR}    env:PATH=${OUTPUT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=bench_stdlib failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    [BENCH]
    Should Contain    ${run.stdout}    bench_vec_push_pop
    Should Contain    ${run.stdout}    bench_quickmap_hash_set_get
    Should Contain    ${run.stdout}    bench_quickmap_vec_set_get

Type Inference Regression
    ${run}=    Run Process    ${MLANG}    test    ${EXECDIR}/tests/type_inference_tests.mla
    ...    stdout=PIPE    stderr=PIPE    cwd=${OUTPUT DIR}    env:PATH=${OUTPUT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0    msg=Type inference regression failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Closures Demo Runs Correctly
    [Documentation]    Build and run examples/closures_demo.mla, verify key
    ...                output lines for compound assignment, inline capturing
    ...                closures, and thread::spawn with a closure literal.
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/closures_demo_bin
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
    ...    stdout=PIPE    stderr=PIPE    cwd=${OUTPUT DIR}    env:PATH=${OUTPUT DIR}:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=closure_tests failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pass=17

Inline Attrs Demo Runs Correctly
    [Documentation]    Build and run examples/inline_attrs.mla; verify that
    ...                #[inline], #[inline(always)], and #[inline(never)]
    ...                compile and produce correct output.
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/inline_attrs_bin
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building mlang frontend wrapper (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=Frontend wrapper failed forwarding --version (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    mlang-frontend-mla

MLang Frontend Test Version Uses Backend Semantics
    [Documentation]    Verify `test --version` is passed through and reports backend version semantics.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_version_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_help_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_test_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend NoRun Before TrailingTests Fails
    [Documentation]    Verify left-to-right C++ parity: `--no-run` before trailing `--tests` is an unknown option.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_norun_before_trailing_tests
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_norun_before_trailing_tests.mla
    ${code}=    Catenate    SEPARATOR=\n
    ...    fn main() -> i32 {
    ...        return 0;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}    --no-run    --tests    ${src}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --no-run

MLang Frontend Test Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in test mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_test_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test ShortHelp Before Unknown Succeeds
    [Documentation]    Verify argument order parity: -h before unknown option in test mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_test_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Test Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in test mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_test_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Test Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in test mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_test_unknown_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    test    --definitely-unknown-flag    --version
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend RunTests Help Uses Backend Semantics
    [Documentation]    Verify `run tests --help` is passed through and uses backend help text.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_help_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend RunTests Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in run tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_help_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --help    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests ShortHelp Before Unknown Succeeds
    [Documentation]    Verify argument order parity: -h before unknown option in run tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend RunTests Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in run tests mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    run    tests    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend RunTests Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in run tests mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_unknown_version_order
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_help_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_version_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_valuepos_version
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_warmup_valuepos_help
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_version_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_shorthelp_passthrough
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_unknown_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --definitely-unknown-flag    --help
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:

MLang Frontend Bench Help Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --help before unknown option in bench mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_help_order
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_shorthelp_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    -h    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    Usage:

MLang Frontend Bench Version Before Unknown Succeeds
    [Documentation]    Verify argument order parity: --version before unknown option in bench mode should succeed.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_version_order
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${MLANG}
    ...    bench    --version    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stdout}    mlang

MLang Frontend Bench Unknown Before Version Fails
    [Documentation]    Verify argument order parity: unknown option before --version in bench mode should fail.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_unknown_version_order
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_dispatch
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ...    msg=Failed building frontend wrapper (dispatch) (rc=${build.rc})\nSTDOUT:\n${build.stdout}\nSTDERR:\n${build.stderr}
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_dispatch_suite
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

MLang Frontend RunTests Directory Forwards NoRun
    [Documentation]    Verify `run tests <dir> --no-run` forwards --no-run to each suite invocation.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_runtests_norun
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_runtests_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_runtests_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_runtests_norun_backend.log
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

MLang Frontend Tests Flag Works In Trailing Position
    [Documentation]    Verify `<file> --tests` activates test mode even when --tests is not the first arg.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_tests_trailing
    ${build}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_tests_trailing.mla
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

MLang Binary Frontend Env Switch Works
    [Documentation]    Verify `MLANG_FRONTEND_IMPL=mla` routes `mlang` through the MLang frontend implementation.
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_env_switch.mla
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_env_switch_bin
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_pkg
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ...    msg=Failed building frontend wrapper (pkg dispatch) (rc=${build_front.rc})\nSTDOUT:\n${build_front.stdout}\nSTDERR:\n${build_front.stderr}
    ${r1}=    Run Process    ${frontend}    --backend    /bin/echo    pkg    init
    ...    env:MLANG_PKG_IMPL=cpp    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${r1.rc}    0
    ...    msg=frontend pkg init with cpp backend failed (rc=${r1.rc})\nSTDOUT:\n${r1.stdout}\nSTDERR:\n${r1.stderr}
    Should Contain    ${r1.stdout}    pkg init

MLang Frontend Pkg Unknown Impl Uses Cpp Fallback
    [Documentation]    Verify MLANG_PKG_IMPL unknown values do not prefer MLang pkg frontend (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_pkg_unknown
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    /bin/echo    pkg    init
    ...    env:MLANG_PKG_IMPL=unknown    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=unknown MLANG_PKG_IMPL should route pkg to backend directly (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pkg init

MLang Frontend Empty Test Dir Fails
    [Documentation]    Verify frontend parity with C++ main: `test <empty_dir>` returns nonzero.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_emptydir
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ...    msg=Failed building frontend wrapper (empty dir parity) (rc=${build_front.rc})\nSTDOUT:\n${build_front.stdout}\nSTDERR:\n${build_front.stderr}
    ${empty}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_empty_suite_dir
    Create Directory    ${empty}
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang    test    ${empty}
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Error: No .mla test files found in

MLang Frontend Test Uses Last Positional Path
    [Documentation]    Verify frontend test-mode positional parsing matches C++ main semantics (last positional wins).
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_lastpos
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_lastpos_suite
    ${empty_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_lastpos_empty
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

MLang Frontend Skips Synthetic Test Root Files
    [Documentation]    Verify frontend test directory mode ignores __mlang_test_root*.mla files during suite discovery.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_skiproot
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_skip_root_suite
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_modonly
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_modonly_suite
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_failnorm
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_fail_norm_suite
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_parse
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

MLang Frontend Bench Flag Validation
    [Documentation]    Verify frontend bench mode reports invalid numeric values for bench flags.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_validate
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_inline
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

MLang Frontend Bench Warmup Validation
    [Documentation]    Verify frontend rejects non-numeric warmup values before backend compile.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_warmup_validate
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_range_validate
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_i32_bounds
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/bench_i32_bounds.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_i32_bounds_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_i32_bounds_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_warmup_signed
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    5    --bench-warmup    -1    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0

MLang Frontend Bench Accepts Zero Iterations Value
    [Documentation]    Verify frontend accepts numeric zero for --bench-iters (backend applies C++ clamp).
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_iters_zero
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters    0    --bench-warmup    0    ${EXECDIR}/tests/bench_stdlib.mla
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0

MLang Frontend Bench Directory Forwards Default Iteration Flags
    [Documentation]    Verify frontend bench directory mode forwards C++ default --bench-iters/--bench-warmup when not provided.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_defaults
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_bench_defaults_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_single_defaults
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/bench_single_defaults.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_single_defaults_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_single_defaults_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_clamp_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/bench_clamp_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_clamp_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_clamp_single_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_lastflags
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_bench_lastflags_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_lastflags_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_lastflags_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_canon
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_bench_canon_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_canon_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_canon_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_clamp
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_bench_clamp_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_clamp_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_clamp_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_norun
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_bench_norun_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_norun_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_norun_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_norun_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/bench_norun_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_norun_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_norun_single_backend.log
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

MLang Frontend Bench Ignores Colon Warning Flags
    [Documentation]    Verify bench mode does not forward -Wno-colon-if/-Wno-colon-while to backend suite invocations.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_nowarn
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_bench_nowarn_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_nowarn_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_nowarn_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_warn_single
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${bench_file}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/bench_warn_single.mla
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_warn_single_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_bench_warn_single_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_dir_igno
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_dir_ignore_o_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_dir_ignore_o_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_dir_ignore_o_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_dir_compact_link
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_dir_compact_link_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_dir_compact_link_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_dir_compact_link_backend.log
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
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_dir_wl
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_dir_wl_suite
    ${fake_backend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_dir_wl_backend.sh
    ${fake_log}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/fake_dir_wl_backend.log
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

MLang Frontend Directory Mode Rejects Unknown Option
    [Documentation]    Verify test/bench directory mode rejects unknown options instead of silently dropping them.
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_dir_unknown
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${suite_dir}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_dir_unknown_suite
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

MLang Frontend Bench Missing Value Uses Unknown Option Error
    [Documentation]    Verify missing value for bench options is reported as unknown option (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_bench_missing
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    --bench-iters
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option: --bench-iters

MLang Frontend Missing LinkOrOutput Value Uses Unknown Option Error
    [Documentation]    Verify missing value for -o/-L/-l in test mode reports unknown option and usage (C++ parity).
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_missing_link_or_output
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/frontend_missing_link_or_output.mla
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

MLang Frontend Unknown Option Prints Usage
    [Documentation]    Verify unknown test/bench options print usage text in addition to error (C++ parity style).
    ${frontend}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/mlang_frontend_mla_bin_unknown_usage
    ${build_front}=    Run Process    ${MLANG}    tools/mlang-frontend-mla/main.mla    -L    ./build    -lmlang_std    -o    ${frontend}
    ...    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${build_front.rc}    0
    ${run}=    Run Process    ${frontend}    --backend    ${EXECDIR}/build/mlang
    ...    bench    ${EXECDIR}/tests/bench_stdlib.mla    --definitely-unknown-flag
    ...    stdout=PIPE    stderr=PIPE
    Should Not Be Equal As Integers    ${run.rc}    0
    Should Contain    ${run.stderr}    Unknown option:
    Should Contain    ${run.stdout}    Usage:

Testing Mock Example Runs Correctly
    [Documentation]    Build and run examples/testing_mock_example.mla and verify
    ...                std::testing mock expectations pass.
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/testing_mock_example_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/argparser_demo_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/slice_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/lambda_fold_patterns_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/lambda_fold_advanced_bin
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
    ${src}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/printf_demo.mla
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/printf_demo_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_fs_lines_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_fs_seek_bin
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
    ${bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_fs_rw_bin
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
    ${server_bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_net_mt_server_bin
    ${client_bin}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_net_mt_client_bin
    ${server_out}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_net_mt_server.out
    ${server_err}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/std_net_mt_server.err
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

Pkg Fetch Build Parity (CPP vs MLA)
    [Documentation]    Create a local git C dependency and verify both
    ...                package-manager backends (MLANG_PKG_IMPL=cpp|mla)
    ...                pass init/add/fetch/build and produce runnable binaries.
    ${base}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/pkg_parity
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
    ${base}=    Catenate    SEPARATOR=    ${OUTPUT DIR}/pkg_cfg_parity
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
