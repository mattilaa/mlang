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
...    examples/pointer_access.mla
...    examples/print_test.mla
...    examples/result_match.mla
...    examples/result_usage.mla
...    examples/slice.mla
...    examples/str_types.mla
...    examples/ternary_example.mla
...    examples/thread_basic.mla
...    examples/thread_multi.mla
...    examples/thread_mutex_atomic.mla
...    examples/tuple_example.mla
...    examples/tuple_test.mla

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
    ...    #[test]
    ...    fn test_ok() -> i32 {
    ...        return 0;
    ...    }
    ...    #[test]
    ...    fn test_fail() -> i32 {
    ...        return 1;
    ...    }
    Create File    ${src}    ${code}
    ${run}=    Run Process    ${MLANG}    test    ${src}    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    1    msg=Expected 1 failing test, got ${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Mlang Test Sample Directory
    ${run}=    Run Process    ${MLANG}    test    tests    stdout=PIPE    stderr=PIPE
    Should Be Equal As Integers    ${run.rc}    0    msg=Expected sample tests to pass, got ${run.rc}\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}

Type Inference Regression
    ${run}=    Run Process    ${MLANG}    test    tests/type_inference_tests.mla    stdout=PIPE    stderr=PIPE    env:PATH=.:%{PATH}
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
    ${run}=    Run Process    ${MLANG}    test    tests/closure_tests.mla
    ...    stdout=PIPE    stderr=PIPE    env:PATH=.:%{PATH}
    Should Be Equal As Integers    ${run.rc}    0
    ...    msg=closure_tests failed (rc=${run.rc})\nSTDOUT:\n${run.stdout}\nSTDERR:\n${run.stderr}
    Should Contain    ${run.stdout}    pass=17

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
