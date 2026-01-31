*** Settings ***
Library           Process
Library           OperatingSystem

*** Variables ***
${MLANG}           ./build/mlang
@{EXAMPLES}
...    examples/break_continue.mla
...    examples/c_lib_usage.mla
...    examples/c_type_mappings.mla
...    examples/debug_test.mla
...    examples/enum_option_match.mla
...    examples/ffi_add.mla
...    examples/ffi_cos.mla
...    examples/for_loop_example.mla
...    examples/generics_test.mla
...    examples/main.mla
...    examples/map_iteration.mla
...    examples/map_list.mla
...    examples/math.mla
...    examples/print_test.mla
...    examples/result_match.mla
...    examples/str_types.mla
...    examples/thread_basic.mla
...    examples/thread_multi.mla
...    examples/thread_mutex_atomic.mla
...    examples/tuple_example.mla
...    examples/tuple_test.mla
...    examples/package_manager_git_cjson/src/main.mla

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
