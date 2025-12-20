# MLA Language Test Suite
#
# This can be used in two ways:
# 1. As part of the main build: included via add_subdirectory(tests) from parent
# 2. Standalone: cmake -DMLA_COMPILER=/path/to/mylang .
#
cmake_minimum_required(VERSION 3.14)

# Check if we're being included from parent or built standalone
if(NOT TARGET mylang)
    # Standalone build
    project(MLATests)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # User must provide compiler path
    set(MLA_COMPILER "" CACHE FILEPATH "Path to the mylang compiler executable")

    if(NOT MLA_COMPILER)
        # Try to find it in common locations
        find_program(MLA_COMPILER_FOUND mylang
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
                "Could not find mylang compiler. Please specify with -DMLA_COMPILER=/path/to/mylang\n"
                "Or build from the project root with: cmake -DBUILD_TESTS=ON .."
            )
        endif()
    endif()

    message(STATUS "Using MLA compiler: ${MLA_COMPILER}")

    # Verify compiler exists
    if(NOT EXISTS ${MLA_COMPILER})
        message(FATAL_ERROR "MLA compiler not found at: ${MLA_COMPILER}")
    endif()

    set(STANDALONE_BUILD TRUE)
    enable_testing()
endif()

# Include FetchContent for downloading GoogleTest
include(FetchContent)

# Fetch GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Create the test executable
add_executable(mla_tests
    mla_tests.cpp
)

# Link GoogleTest
target_link_libraries(mla_tests
    GTest::gtest
    GTest::gtest_main
)

# Set C++ standard
target_compile_features(mla_tests PRIVATE cxx_std_17)

# Pass the compiler path to the test executable
if(STANDALONE_BUILD)
    target_compile_definitions(mla_tests PRIVATE
        DEFAULT_COMPILER_PATH="${MLA_COMPILER}"
    )
else()
    # When built as part of main project, use the built mylang
    target_compile_definitions(mla_tests PRIVATE
        DEFAULT_COMPILER_PATH="$<TARGET_FILE:mylang>"
    )
    # Make sure mylang is built before tests
    add_dependencies(mla_tests mylang)
endif()

# Add test discovery
include(GoogleTest)

if(STANDALONE_BUILD)
    gtest_discover_tests(mla_tests
        PROPERTIES
            ENVIRONMENT "MLA_COMPILER=${MLA_COMPILER}"
    )
else()
    gtest_discover_tests(mla_tests
        PROPERTIES
            ENVIRONMENT "MLA_COMPILER=$<TARGET_FILE:mylang>"
    )
endif()

# Custom target to run tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS mla_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running MLA tests..."
)
