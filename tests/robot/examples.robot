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
