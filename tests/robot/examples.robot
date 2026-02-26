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
    ...    #[test]
    ...    fn test_ok() -> i32 {
    ...        return 0;
    ...    }
    ...    #[test]
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
    Should Contain    ${run.stdout}    clamp(-3, 0..10): 0
    Should Contain    ${run.stdout}    clamp(5,  0..10): 5
    Should Contain    ${run.stdout}    sum of clamp(i,2..4)^2 for i=1..5: 49

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
    ...    #!/bin/sh
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
