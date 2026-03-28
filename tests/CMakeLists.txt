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

option(BUILD_CPP_GTESTS "Build GoogleTest-based C++ compiler tests" ON)

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
            LABELS "mla"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()

add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L mla
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running MLang-native test suites..."
)
if(TARGET mlang)
    add_dependencies(run_tests mlang)
endif()

if(BUILD_CPP_GTESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(mla_tests mla_tests.cpp)
    target_compile_features(mla_tests PRIVATE cxx_std_17)
    target_include_directories(mla_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(mla_tests PRIVATE
        DEFAULT_COMPILER_PATH="$<TARGET_FILE:mlang>"
        DEFAULT_MLANG_LIB_DIR="${MLANG_LIB_DIR}"
    )
    target_link_libraries(mla_tests PRIVATE GTest::gtest GTest::gtest_main)

    if(TARGET mlang)
        add_dependencies(mla_tests mlang mlang_std)
    endif()

    include(GoogleTest)
    gtest_discover_tests(mla_tests
        DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        PROPERTIES
            LABELS "cpp"
            ENVIRONMENT "MLA_COMPILER=${MLA_COMPILER_CMD};MLANG_STDLIB_LIB_PATH=${MLANG_LIB_DIR}"
    )

    add_custom_target(run_cpp_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -L cpp
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running GoogleTest-based C++ compiler tests..."
    )
    add_dependencies(run_cpp_tests mla_tests)

    add_custom_target(run_all_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running all MLang and GoogleTest suites..."
    )
    add_dependencies(run_all_tests mla_tests)
    if(TARGET mlang)
        add_dependencies(run_all_tests mlang)
    endif()
endif()
