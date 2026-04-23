cmake_minimum_required(VERSION 3.20)
set(MLANG_VERSION_MAJOR 0)
set(MLANG_VERSION_MINOR 2)
option(MLANG_AUTO_INCREMENT_BUILD "Auto-increment build number on each CMake configure" ON)
set(MLANG_VERSION_BUILD 0 CACHE STRING "Build number")
set(MLANG_BUILD_NUMBER_FILE "${CMAKE_BINARY_DIR}/.mlang_build_number" CACHE FILEPATH
    "Path to persisted auto-increment build number")

if(MLANG_AUTO_INCREMENT_BUILD)
    set(_mlang_seed "${MLANG_VERSION_BUILD}")
    if(EXISTS "${MLANG_BUILD_NUMBER_FILE}")
        file(READ "${MLANG_BUILD_NUMBER_FILE}" _mlang_build_raw)
        string(STRIP "${_mlang_build_raw}" _mlang_build_raw)
        if(_mlang_build_raw MATCHES "^[0-9]+$")
            set(_mlang_seed "${_mlang_build_raw}")
        endif()
    endif()

    if(NOT _mlang_seed MATCHES "^[0-9]+$")
        set(_mlang_seed 0)
    endif()

    math(EXPR MLANG_VERSION_BUILD "${_mlang_seed} + 1")
    set(MLANG_VERSION_BUILD "${MLANG_VERSION_BUILD}" CACHE STRING "Build number" FORCE)
    file(WRITE "${MLANG_BUILD_NUMBER_FILE}" "${MLANG_VERSION_BUILD}\n")
endif()

project(MLang VERSION ${MLANG_VERSION_MAJOR}.${MLANG_VERSION_MINOR}.${MLANG_VERSION_BUILD})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

# Install location for stdlib modules.
set(MLANG_STDLIB_INSTALL_DIR "${CMAKE_INSTALL_FULL_DATADIR}/mlang/stdlib")
add_compile_definitions(MLANG_STDLIB_INSTALL_DIR="${MLANG_STDLIB_INSTALL_DIR}")
add_compile_definitions(MLANG_STDLIB_SOURCE_DIR="${CMAKE_SOURCE_DIR}/stdlib")

# Install location for stdlib libraries.
set(MLANG_STDLIB_LIB_INSTALL_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/mlang")
add_compile_definitions(MLANG_STDLIB_LIB_INSTALL_DIR="${MLANG_STDLIB_LIB_INSTALL_DIR}")

# Create compile_commands.json in build directory
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Find LLVM
# ============================================================================
# Try to find LLVM, with hints for common installation paths.
#
# NOTE (Windows): The LLVM/Clang toolset bundled inside Visual Studio is a
# stripped-down clang-cl distribution that does NOT ship LLVMConfig.cmake or
# the individual LLVM component libraries.  A full, standalone LLVM SDK is
# required.  Install one with:
#     winget install LLVM.LLVM
# or download from https://github.com/llvm/llvm-project/releases
# The installer places files under C:\Program Files\LLVM by default.

# Allow user to specify LLVM_DIR
if(NOT DEFINED LLVM_DIR)
    # -- Collect candidate LLVM root directories (for llvm-config + prefix path)
    set(_llvm_candidate_roots "")

    if(WIN32)
        # 1. Standalone LLVM installer (highest priority on Windows)
        list(APPEND _llvm_candidate_roots "C:/Program Files/LLVM")

        # 2. Visual Studio 2022 bundled LLVM — probe via vswhere
        set(_vswhere_path "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
        if(EXISTS "${_vswhere_path}")
            execute_process(
                COMMAND "${_vswhere_path}"
                    -latest -products * -property installationPath
                    -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang
                OUTPUT_VARIABLE _vs_llvm_install_path
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_vs_llvm_install_path)
                message(STATUS "vswhere found VS with LLVM at: ${_vs_llvm_install_path}")
                list(APPEND _llvm_candidate_roots
                    "${_vs_llvm_install_path}/VC/Tools/Llvm/x64"
                    "${_vs_llvm_install_path}/VC/Tools/Llvm"
                )
            endif()
        endif()

        # 3. Hardcoded fallback paths for all VS 2022 editions
        foreach(_vs_ed Professional Enterprise Community BuildTools Preview)
            list(APPEND _llvm_candidate_roots
                "C:/Program Files/Microsoft Visual Studio/2022/${_vs_ed}/VC/Tools/Llvm/x64"
                "C:/Program Files/Microsoft Visual Studio/2022/${_vs_ed}/VC/Tools/Llvm"
            )
        endforeach()
    endif()

    # Unix / macOS candidate roots
    list(APPEND _llvm_candidate_roots
        /opt/homebrew/opt/llvm          # macOS Homebrew (Apple Silicon)
        /usr/local/opt/llvm             # macOS Homebrew (Intel)
        /usr/lib/llvm-20                # Ubuntu/Debian LLVM 20
        /usr/lib/llvm-19                # Ubuntu/Debian LLVM 19
        /usr/lib/llvm-18                # Ubuntu/Debian LLVM 18
        /usr/lib/llvm-17                # Ubuntu/Debian LLVM 17
        /usr/lib/llvm-16                # Ubuntu/Debian LLVM 16
        /usr/lib/llvm-15                # Ubuntu/Debian LLVM 15
        /usr/local                      # Manual installation
    )

    # -- Build a list of bin/ paths for llvm-config search
    set(_llvm_bin_hints "")
    foreach(_root ${_llvm_candidate_roots})
        list(APPEND _llvm_bin_hints "${_root}/bin")
    endforeach()
    list(APPEND _llvm_bin_hints /usr/bin /usr/local/bin)

    find_program(LLVM_CONFIG_EXE
        NAMES llvm-config llvm-config-20 llvm-config-19 llvm-config-18
              llvm-config-17 llvm-config-16 llvm-config-15
        PATHS ${_llvm_bin_hints}
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

# -- Build CMAKE_PREFIX_PATH from candidates that actually contain LLVMConfig.cmake
# This avoids adding the stripped VS-bundled LLVM that lacks the CMake config.
foreach(_root ${_llvm_candidate_roots})
    if(EXISTS "${_root}/lib/cmake/llvm/LLVMConfig.cmake")
        message(STATUS "LLVM candidate validated: ${_root}")
        list(APPEND CMAKE_PREFIX_PATH "${_root}")
    endif()
endforeach()

# Also add all candidates unconditionally as a last-resort fallback, so that
# find_package can still locate non-standard layouts.
list(APPEND CMAKE_PREFIX_PATH ${_llvm_candidate_roots})

find_package(LLVM CONFIG)

if(NOT LLVM_FOUND)
    if(WIN32)
        message(FATAL_ERROR
            "Could not find a full LLVM development SDK (LLVMConfig.cmake).\n"
            "\n"
            "The LLVM/Clang toolset bundled inside Visual Studio is a stripped-down\n"
            "clang-cl distribution and does NOT include the LLVM development libraries.\n"
            "\n"
            "Please install the full LLVM SDK using ONE of:\n"
            "  winget install LLVM.LLVM\n"
            "  choco install llvm\n"
            "  Download from https://github.com/llvm/llvm-project/releases\n"
            "\n"
            "The default install path C:\\Program Files\\LLVM will be found automatically.\n"
            "Or set -DLLVM_DIR=<path>/lib/cmake/llvm on the CMake command line."
        )
    else()
        message(FATAL_ERROR
            "Could not find LLVM (LLVMConfig.cmake). Install the LLVM development\n"
            "package for your system, or set -DLLVM_DIR=<path>/lib/cmake/llvm."
        )
    endif()
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# ============================================================================
# DIA SDK (Windows only) — LLVM links diaguids.lib for PDB debug-info support.
# The pre-built LLVM package may hard-code a VS edition path (e.g. Enterprise)
# that differs from the locally installed edition.  Detect the correct path
# and patch the LLVM target property so the linker can find diaguids.lib.
# ============================================================================
if(WIN32 AND TARGET LLVMDebugInfoPDB)
    set(_dia_diaguids_path "")

    # 1. Probe via vswhere for the active VS installation
    set(_vswhere "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer/vswhere.exe")
    if(EXISTS "${_vswhere}")
        execute_process(
            COMMAND "${_vswhere}" -latest -products * -property installationPath
            OUTPUT_VARIABLE _vs_install_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_vs_install_path AND EXISTS "${_vs_install_path}/DIA SDK/lib/amd64/diaguids.lib")
            set(_dia_diaguids_path "${_vs_install_path}/DIA SDK/lib/amd64/diaguids.lib")
            message(STATUS "DIA SDK found via vswhere: ${_dia_diaguids_path}")
        endif()
    endif()

    # 2. Fallback: scan all VS 2022 editions
    if(NOT _dia_diaguids_path)
        foreach(_ed Professional Enterprise Community BuildTools Preview)
            set(_candidate "C:/Program Files/Microsoft Visual Studio/2022/${_ed}/DIA SDK/lib/amd64/diaguids.lib")
            if(EXISTS "${_candidate}")
                set(_dia_diaguids_path "${_candidate}")
                message(STATUS "DIA SDK found at: ${_dia_diaguids_path}")
                break()
            endif()
        endforeach()
    endif()

    # Patch the hardcoded path in LLVMDebugInfoPDB's link libraries
    if(_dia_diaguids_path)
        get_target_property(_pdb_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        if(_pdb_libs)
            set(_pdb_libs_fixed "")
            foreach(_lib ${_pdb_libs})
                if(_lib MATCHES "diaguids\\.lib$")
                    list(APPEND _pdb_libs_fixed "${_dia_diaguids_path}")
                else()
                    list(APPEND _pdb_libs_fixed "${_lib}")
                endif()
            endforeach()
            set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${_pdb_libs_fixed}")
            message(STATUS "Patched LLVMDebugInfoPDB diaguids.lib path")
        endif()
    else()
        message(WARNING "DIA SDK not found - linking diaguids.lib may fail. "
                        "Install the 'C++ ATL' workload in Visual Studio.")
    endif()
endif()

# ============================================================================
# Find Flex and Bison
# ============================================================================
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
# ============================================================================
# Find OpenSSL
# ============================================================================
# On Windows, probe common standalone OpenSSL install locations so that
# find_package can succeed without the user setting OPENSSL_ROOT_DIR.
if(WIN32 AND NOT DEFINED OPENSSL_ROOT_DIR)
    # Candidate roots ordered by priority
    set(_openssl_candidate_roots "")

    # FireDaemon OpenSSL (winget install FireDaemon.OpenSSL)
    file(GLOB _fd_dirs "C:/Program Files/FireDaemon OpenSSL*")
    list(APPEND _openssl_candidate_roots ${_fd_dirs})

    # ShiningLight OpenSSL (winget install ShiningLight.OpenSSL)
    list(APPEND _openssl_candidate_roots
        "C:/Program Files/OpenSSL-Win64"
        "C:/Program Files/OpenSSL"
    )

    # Chocolatey OpenSSL
    list(APPEND _openssl_candidate_roots
        "C:/tools/OpenSSL-Win64"
        "C:/tools/OpenSSL"
    )

    foreach(_root ${_openssl_candidate_roots})
        if(EXISTS "${_root}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_root}")
            message(STATUS "Auto-detected OpenSSL at: ${OPENSSL_ROOT_DIR}")
            break()
        endif()
    endforeach()

    if(NOT DEFINED OPENSSL_ROOT_DIR)
        message(STATUS "OpenSSL not found in standard Windows paths; "
                       "set -DOPENSSL_ROOT_DIR=<path> if needed")
    endif()
endif()

find_package(OpenSSL REQUIRED)

get_filename_component(MLANG_OPENSSL_SSL_DIR "${OPENSSL_SSL_LIBRARY}" DIRECTORY)
get_filename_component(MLANG_OPENSSL_CRYPTO_DIR "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
add_compile_definitions(MLANG_OPENSSL_SSL_LIBRARY="${OPENSSL_SSL_LIBRARY}")
add_compile_definitions(MLANG_OPENSSL_CRYPTO_LIBRARY="${OPENSSL_CRYPTO_LIBRARY}")
add_compile_definitions(MLANG_OPENSSL_SSL_DIR="${MLANG_OPENSSL_SSL_DIR}")
add_compile_definitions(MLANG_OPENSSL_CRYPTO_DIR="${MLANG_OPENSSL_CRYPTO_DIR}")

set(HEADER_FILES
    ${CMAKE_SOURCE_DIR}/include/ast.h
    ${CMAKE_SOURCE_DIR}/include/ir.h
    ${CMAKE_SOURCE_DIR}/include/package_manager.h
    ${CMAKE_SOURCE_DIR}/include/incremental_compiler.h
    ${CMAKE_SOURCE_DIR}/include/ide_query.h
    ${CMAKE_SOURCE_DIR}/include/workspace_graph.h
    ${CMAKE_SOURCE_DIR}/include/runtime_concurrency.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_handle_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/package_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/globals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/incremental_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ide_query.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/workspace_graph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_concurrency.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler_api.cpp
    ${LEXER_OUT}
    ${PARSER_OUT}
)

add_executable(mlang ${SOURCE_FILES} ${HEADER_FILES})
target_compile_definitions(mlang PRIVATE MLANG_VERSION="${PROJECT_VERSION}")

# Add dependencies to ensure Flex and Bison run before building the executable
add_dependencies(mlang ParserTarget LexerTarget)

target_include_directories(mlang PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(mlang PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(mlang PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

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

target_link_libraries(mlang ${llvm_libs})

add_library(mlang_std STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_handle_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/globals.cpp
    ${LEXER_OUT}
    ${PARSER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_fft.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_strbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_bytes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_hash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_bitset.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_serde.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_vec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_compiler_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_env.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_rand.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_ascii.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_esc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_chat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_term.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_toml.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_argparser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_regex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_unordered.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_bench.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_testing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_exceptions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_sync.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_io.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_fs.c
)

# These stdlib modules require deep POSIX APIs (sockets, pthreads, pty, fork,
# poll, etc.) and are excluded from the Windows build until full Win32
# implementations are provided.
if(NOT WIN32)
    target_sources(mlang_std PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_process.c
    )
else()
    target_sources(mlang_std PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_process_win.c
    )
endif()
target_sources(mlang_std PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_ssl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_jsonrpc.c
)
target_sources(mlang_std PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_net.c
)
if(WIN32)
    target_link_libraries(mlang_std PRIVATE ws2_32)
endif()
add_dependencies(mlang_std ParserTarget LexerTarget)
target_include_directories(mlang_std PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(mlang_std PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(mlang_std PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mlang_std PROPERTIES OUTPUT_NAME "mlang_std")
target_compile_definitions(mlang_std PRIVATE MLANG_VERSION="${PROJECT_VERSION}")
target_link_libraries(mlang_std PUBLIC OpenSSL::SSL OpenSSL::Crypto)
if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(IMAGEIO_FRAMEWORK ImageIO)
    target_link_libraries(mlang_std PUBLIC
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK}
    )
endif()

# RapidJSON for std::json backend (header-only).
include(FetchContent)
find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h
    PATH_SUFFIXES include
    PATHS
        /opt/homebrew/opt/rapidjson
        /usr/local/opt/rapidjson
        /opt/homebrew
        /usr/local
        /usr
)
if(NOT RAPIDJSON_INCLUDE_DIR)
    message(STATUS "RapidJSON not found in system paths; fetching v1.1.0")
    FetchContent_Declare(
        rapidjson
        GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
        GIT_TAG v1.1.0
        GIT_PROGRESS TRUE
    )
    FetchContent_GetProperties(rapidjson)
    if(NOT rapidjson_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(rapidjson)
    endif()
    set(RAPIDJSON_INCLUDE_DIR ${rapidjson_SOURCE_DIR}/include)
endif()
target_include_directories(mlang_std PRIVATE ${RAPIDJSON_INCLUDE_DIR})

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

install(TARGETS mlang
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS mlang_std
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/mlang
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/mlang)

# Install stdlib modules for editor tooling and go-to-definition.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mlang/stdlib
        FILES_MATCHING PATTERN "*.mla" PATTERN "*.md")

# ============================================================================
# Documentation (Doxygen)
# ============================================================================
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_SOURCE_DIR}/docs/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating documentation with Doxygen"
        VERBATIM
    )
else()
    message(STATUS "Doxygen not found; docs target will not be available")
endif()
