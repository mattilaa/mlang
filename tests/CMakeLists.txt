cmake_minimum_required(VERSION 3.14)

# MLang-native test suites.
# Runs .mla test files directly through the compiler in --tests mode.

if(NOT TARGET mlang)
    project(MLATestsNative)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    set(MLA_COMPILER "" CACHE FILEPATH "Path to mlang compiler executable")
    if(NOT MLA_COMPILER)
        find_program(MLA_COMPILER_FOUND mlang
            PATHS
                ${CMAKE_CURRENT_SOURCE_DIR}/../build
                ${CMAKE_CURRENT_SOURCE_DIR}/..
                /usr/local/bin
                /usr/bin
        )
        if(MLA_COMPILER_FOUND)
            set(MLA_COMPILER ${MLA_COMPILER_FOUND})
        else()
            message(FATAL_ERROR
                "Could not find mlang compiler. Specify -DMLA_COMPILER=/path/to/mlang")
        endif()
    endif()
    if(NOT EXISTS ${MLA_COMPILER})
        message(FATAL_ERROR "MLA compiler not found at: ${MLA_COMPILER}")
    endif()

    set(MLA_COMPILER_CMD ${MLA_COMPILER})
    set(MLANG_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../build)
    enable_testing()
else()
    set(MLA_COMPILER_CMD $<TARGET_FILE:mlang>)
    set(MLANG_LIB_DIR ${CMAKE_BINARY_DIR})
endif()

option(MLA_REGISTER_ALL_MLANG_SUITES
       "Register all *_tests.mla/test_*.mla as CTest suites"
       OFF)

if(MLA_REGISTER_ALL_MLANG_SUITES)
    file(GLOB MLA_TEST_FILES
        RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        "*_tests.mla"
        "test_*.mla"
    )
else()
    set(MLA_TEST_FILES "mla_tests.mla")
endif()

list(SORT MLA_TEST_FILES)

if(MLA_TEST_FILES STREQUAL "")
    message(FATAL_ERROR "No MLang test suite files found")
endif()

foreach(test_file ${MLA_TEST_FILES})
    get_filename_component(suite_name ${test_file} NAME_WE)
    add_test(
        NAME mla_suite_${suite_name}
        COMMAND ${MLA_COMPILER_CMD}
                --tests
                ${CMAKE_CURRENT_SOURCE_DIR}/${test_file}
                -L ${MLANG_LIB_DIR}
                -lmlang_std
    )
    set_tests_properties(mla_suite_${suite_name}
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()

add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running MLang-native test suites..."
)
if(TARGET mlang)
    add_dependencies(run_tests mlang)
endif()

# Python scaffold tests (frontend + LSP transport).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME mlang_frontend_parser
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mlang_frontend_parser_test.py
    )
    add_test(
        NAME mlang_lsp_scaffold_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/mlang_lsp_scaffold_integration_test.py
    )
    set_tests_properties(mlang_frontend_parser mlang_lsp_scaffold_integration
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
