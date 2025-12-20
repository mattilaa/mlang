cmake_minimum_required(VERSION 3.20)
project(MyLanguage)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create compile_commands.json in build directory
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Find LLVM
# ============================================================================
# Try to find LLVM, with hints for common installation paths

# Allow user to specify LLVM_DIR
if(NOT DEFINED LLVM_DIR)
    # Try to find llvm-config and use it to get the cmake dir
    find_program(LLVM_CONFIG_EXE NAMES llvm-config llvm-config-20 llvm-config-29 llvm-config-18 llvm-config-17 llvm-config-16 llvm-config-15
        PATHS
            /opt/homebrew/opt/llvm/bin
            /usr/local/opt/llvm/bin
            /usr/lib/llvm-20/bin
            /usr/lib/llvm-19/bin
            /usr/lib/llvm-18/bin
            /usr/lib/llvm-17/bin
            /usr/lib/llvm-16/bin
            /usr/lib/llvm-15/bin
            /usr/bin
            /usr/local/bin
    )

    if(LLVM_CONFIG_EXE)
        execute_process(
            COMMAND ${LLVM_CONFIG_EXE} --cmakedir
            OUTPUT_VARIABLE LLVM_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        message(STATUS "Found llvm-config: ${LLVM_CONFIG_EXE}")
        message(STATUS "LLVM CMake dir: ${LLVM_DIR}")
    endif()
endif()

# Common LLVM installation paths to search
list(APPEND CMAKE_PREFIX_PATH
    /opt/homebrew/opt/llvm          # macOS Homebrew (Apple Silicon)
    /usr/local/opt/llvm             # macOS Homebrew (Intel)
    /usr/lib/llvm-20                # Ubuntu/Debian LLVM 18
    /usr/lib/llvm-19                # Ubuntu/Debian LLVM 18
    /usr/lib/llvm-18                # Ubuntu/Debian LLVM 18
    /usr/lib/llvm-17                # Ubuntu/Debian LLVM 17
    /usr/lib/llvm-16                # Ubuntu/Debian LLVM 16
    /usr/lib/llvm-15                # Ubuntu/Debian LLVM 15
    /usr/local                      # Manual installation
    "C:/Program Files/LLVM"         # Windows
)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# ============================================================================
# Find Flex and Bison
# ============================================================================
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

set(HEADER_FILES
    ${CMAKE_SOURCE_DIR}/include/ast.h
    ${CMAKE_SOURCE_DIR}/include/ir.h
)

# Specify the full path to the lexer and parser files
set(LEXER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.l)
set(PARSER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/parser.y)

# Set output file names
set(LEXER_OUT ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)
set(PARSER_OUT ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp)
set(PARSER_HEADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/parser.hpp)

# Print Flex and Bison information
message(STATUS "Flex executable: ${FLEX_EXECUTABLE}")
message(STATUS "Bison executable: ${BISON_EXECUTABLE}")

# Add custom command and target for Bison
add_custom_command(
    OUTPUT ${PARSER_OUT} ${PARSER_HEADER_OUT}
    COMMAND ${CMAKE_COMMAND} -E echo "Running Bison on ${PARSER_SOURCE}"
    COMMAND ${BISON_EXECUTABLE}
    ARGS -d -o ${PARSER_OUT} --defines=${PARSER_HEADER_OUT} ${PARSER_SOURCE}
    DEPENDS ${PARSER_SOURCE}
    COMMENT "Generating parser.cpp and parser.hpp"
    VERBATIM
)

add_custom_target(
    ParserTarget ALL
    DEPENDS ${PARSER_OUT} ${PARSER_HEADER_OUT}
)

# Add custom command and target for Flex
add_custom_command(
    OUTPUT ${LEXER_OUT}
    COMMAND ${CMAKE_COMMAND} -E echo "Running Flex on ${LEXER_SOURCE}"
    COMMAND ${FLEX_EXECUTABLE}
    ARGS -o ${LEXER_OUT} ${LEXER_SOURCE}
    DEPENDS ${LEXER_SOURCE} ${PARSER_HEADER_OUT}
    COMMENT "Generating lexer.cpp"
    VERBATIM
)

add_custom_target(
    LexerTarget ALL
    DEPENDS ${LEXER_OUT}
)

# Add main.cpp and the generated files to the list of source files
set(SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/globals.cpp
    ${LEXER_OUT}
    ${PARSER_OUT}
)

add_executable(mylang ${SOURCE_FILES} ${HEADER_FILES})

# Add dependencies to ensure Flex and Bison run before building the executable
add_dependencies(mylang ParserTarget LexerTarget)

target_include_directories(mylang PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(mylang PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(mylang PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Link against LLVM libraries - include all components needed for backend
# Core components for IR generation
# Target components for machine code generation
llvm_map_components_to_libnames(llvm_libs
    support
    core
    irreader
    bitwriter
    passes
    # Native target support
    native
    nativecodegen
    # All targets (for cross-compilation support)
    AllTargetsAsmParsers
    AllTargetsCodeGens
    AllTargetsDescs
    AllTargetsInfos
)

target_link_libraries(mylang ${llvm_libs})

# Print debug information
message(STATUS "LEXER_OUT: ${LEXER_OUT}")
message(STATUS "PARSER_OUT: ${PARSER_OUT}")
message(STATUS "PARSER_HEADER_OUT: ${PARSER_HEADER_OUT}")
message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "LLVM Libraries: ${llvm_libs}")

# Force generation at CMake time (use with caution)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${BISON_EXECUTABLE} -d -o ${PARSER_OUT} --defines=${PARSER_HEADER_OUT} ${PARSER_SOURCE}
    COMMAND ${FLEX_EXECUTABLE} -o ${LEXER_OUT} ${LEXER_SOURCE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# ============================================================================
# Testing
# ============================================================================
option(BUILD_TESTS "Build the test suite" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
