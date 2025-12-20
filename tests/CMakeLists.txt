# MLA Language Test Suite
# This file is meant to be included from the main CMakeLists.txt via add_subdirectory(tests)

cmake_minimum_required(VERSION 3.20)
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
# The mylang executable will be in the build directory
target_compile_definitions(mla_tests PRIVATE
    DEFAULT_COMPILER_PATH="$<TARGET_FILE:mylang>"
)

# Make sure mylang is built before tests
add_dependencies(mla_tests mylang)

# Add test discovery
include(GoogleTest)
gtest_discover_tests(mla_tests
    PROPERTIES
        ENVIRONMENT "MLA_COMPILER=$<TARGET_FILE:mylang>"
)

# Custom target to run tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS mla_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running MLA tests..."
)
