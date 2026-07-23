cmake_minimum_required(VERSION 3.20)

# On macOS, default the deployment target and SDK to match the host so the
# link line is consistent with Homebrew LLVM (which is built against the
# host's current macOS SDK). Without this, ld emits noisy "object file was
# built for newer macOS version" warnings whenever Homebrew LLVM is newer
# than the SDK Xcode picks by default. Both vars must be set before
# project() to take effect; user-supplied values are respected.
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
       CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        execute_process(
            COMMAND sw_vers -productVersion
            OUTPUT_VARIABLE _mlang_host_macos_version
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_mlang_host_macos_version MATCHES "^([0-9]+)")
            set(CMAKE_OSX_DEPLOYMENT_TARGET "${CMAKE_MATCH_1}"
                CACHE STRING "macOS deployment target" FORCE)
            message(STATUS
                "Auto-set CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET} "
                "to match host (${_mlang_host_macos_version})")
        endif()
    endif()

    if((NOT DEFINED CMAKE_OSX_SYSROOT OR CMAKE_OSX_SYSROOT STREQUAL "") AND
       DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND
       NOT CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        # Prefer the CommandLineTools SDK that matches the host major version,
        # since Xcode-shipped SDKs may lag behind the host OS.
        string(REGEX MATCH "^[0-9]+" _mlang_target_major
               "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(_mlang_clt_sdk
            "/Library/Developer/CommandLineTools/SDKs/MacOSX${_mlang_target_major}.sdk")
        if(EXISTS "${_mlang_clt_sdk}")
            set(CMAKE_OSX_SYSROOT "${_mlang_clt_sdk}"
                CACHE STRING "macOS SDK" FORCE)
            message(STATUS "Auto-set CMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
        endif()
    endif()
endif()

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

set(_mlang_home_dir "$ENV{HOME}")
if(WIN32 AND NOT "$ENV{USERPROFILE}" STREQUAL "")
    set(_mlang_home_dir "$ENV{USERPROFILE}")
endif()
if(NOT _mlang_home_dir STREQUAL "")
    if(CMAKE_INSTALL_PREFIX STREQUAL "~")
        set(CMAKE_INSTALL_PREFIX "${_mlang_home_dir}" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
    elseif(CMAKE_INSTALL_PREFIX MATCHES "^~/")
        string(SUBSTRING "${CMAKE_INSTALL_PREFIX}" 1 -1
            _mlang_install_prefix_suffix)
        set(_mlang_expanded_install_prefix
            "${_mlang_home_dir}${_mlang_install_prefix_suffix}")
        set(CMAKE_INSTALL_PREFIX "${_mlang_expanded_install_prefix}" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
    elseif(WIN32 AND CMAKE_INSTALL_PREFIX MATCHES "^~\\\\")
        string(SUBSTRING "${CMAKE_INSTALL_PREFIX}" 1 -1
            _mlang_install_prefix_suffix)
        set(_mlang_expanded_install_prefix
            "${_mlang_home_dir}${_mlang_install_prefix_suffix}")
        set(CMAKE_INSTALL_PREFIX "${_mlang_expanded_install_prefix}" CACHE PATH
            "Install path prefix, prepended onto install directories." FORCE)
    endif()
endif()
unset(_mlang_home_dir)
unset(_mlang_install_prefix_suffix)
unset(_mlang_expanded_install_prefix)

include(GNUInstallDirs)

add_executable(mlang-config tools/mlang_config.cpp)
target_compile_features(mlang-config PRIVATE cxx_std_17)
target_compile_definitions(mlang-config PRIVATE
    MLANG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

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
find_package(OpenSSL REQUIRED)
find_program(MLANG_CLANGXX_EXE NAMES clang++-17 clang++ REQUIRED)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/backend_utils.h
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/backend_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/borrows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/builtins.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/enums.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/expression_diagnostics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/expression_type_kind.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/expression_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/formatting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/lifecycle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/literals.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/lvalues.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/moves.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/ownership.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/pointers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/sizeof.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/struct_fields.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/validation.cpp
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
target_include_directories(mlang PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
# `native`/`nativecodegen` overlap with `AllTargets*` on the host arch (e.g.
# AArch64 libs appear once via each), and helper components like Support/Core
# are pulled in transitively after being listed explicitly. Deduplicate the
# explicit list to keep what we control clean.
list(REMOVE_DUPLICATES llvm_libs)

target_link_libraries(mlang ${llvm_libs})

# Apple ld65 still sees duplicates that come in via LLVM's transitive
# INTERFACE_LINK_LIBRARIES (each LLVM*CodeGen has LLVMSupport/LLVMCore etc.
# as INTERFACE deps), and prints "ignoring duplicate libraries" for each.
# It already does the right thing — ignore — so silence the noise.
if(APPLE)
    target_link_options(mlang PRIVATE
        "LINKER:-no_warn_duplicate_libraries")
endif()

# ============================================================================
# mlangd (C++ LSP server)
# ============================================================================
add_executable(mlangd
    tools/mlang_lsp_cpp/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_handle_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_bridge.cpp
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

add_dependencies(mlangd ParserTarget LexerTarget)
target_compile_features(mlangd PRIVATE cxx_std_20)
set_target_properties(mlangd PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(mlangd PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(mlangd PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(mlangd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlangd PRIVATE ${llvm_libs} OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(mlangd PRIVATE
    MLANG_VERSION="${PROJECT_VERSION}"
    MLANG_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(APPLE)
    target_link_libraries(mlangd PRIVATE
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK}
    )
    target_link_options(mlangd PRIVATE
        "LINKER:-no_warn_duplicate_libraries")
endif()

add_library(mlang_std STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_handle_helpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ast_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/globals.cpp
    ${LEXER_OUT}
    ${PARSER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_simd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_fft.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_strbuf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_bytes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_hash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_bitset.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_serde.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_vec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_array.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_compiler_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_io.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_fs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_env.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_rand.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_sync.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_process.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_ipc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_net.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_ssl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_ascii.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_esc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_chat.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_term.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_jsonrpc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_toml.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_argparser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_regex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_unordered.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_bench.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_testing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_exceptions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/src/std_image.c
)
add_dependencies(mlang_std ParserTarget LexerTarget)
target_include_directories(mlang_std PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(mlang_std PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(mlang_std PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(mlang_std PROPERTIES OUTPUT_NAME "mlang_std")
target_compile_definitions(mlang_std PRIVATE MLANG_VERSION="${PROJECT_VERSION}")
target_link_libraries(mlang_std PUBLIC OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(mlang_std PUBLIC ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(mlang_std PUBLIC ws2_32)
endif()
if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(IMAGEIO_FRAMEWORK ImageIO)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    target_link_libraries(mlang_std PUBLIC
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
    )
endif()

# Bootstrap-built MLang formatter.
set(MLANG_FORMAT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tools/mlang-format-mla/main.mla)
set(MLANG_FORMAT_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mlang-format)
set(MLANG_FORMAT_LL_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mlang-format.ll)
set(MLANG_FORMAT_IR_OBJECT_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mlang-format-ir.o)
set(MLANG_FORMAT_STAMP ${CMAKE_CURRENT_BINARY_DIR}/mlang-format.stamp)

add_custom_command(
    OUTPUT ${MLANG_FORMAT_STAMP}
    COMMAND $<TARGET_FILE:mlang> -emit-llvm ${MLANG_FORMAT_SOURCE}
            --no-tests
            -Wno-unwrap
            -O0
            -o ${MLANG_FORMAT_LL_OUTPUT}
    COMMAND ${MLANG_CLANGXX_EXE}
            -x ir
            -c
            ${MLANG_FORMAT_LL_OUTPUT}
            -o ${MLANG_FORMAT_IR_OBJECT_OUTPUT}
    COMMAND ${MLANG_CLANGXX_EXE}
            -o ${MLANG_FORMAT_OUTPUT}
            ${MLANG_FORMAT_IR_OBJECT_OUTPUT}
            -L${CMAKE_CURRENT_BINARY_DIR}
            -lmlang_std
            -lm
            ${OPENSSL_SSL_LIBRARY}
            ${OPENSSL_CRYPTO_LIBRARY}
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:CoreFoundation>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:CoreGraphics>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:ImageIO>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:CoreAudio>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:AudioToolbox>
    COMMAND ${CMAKE_COMMAND} -E touch ${MLANG_FORMAT_STAMP}
    DEPENDS mlang mlang_std ${MLANG_FORMAT_SOURCE}
            ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building mlang-format via LLVM IR + clang++ host link"
    VERBATIM
)

add_custom_target(mlang_format DEPENDS ${MLANG_FORMAT_STAMP})
add_dependencies(mlangd mlang_format)

# Bootstrap-built MLang language server scaffold.
set(MLANGD_MLA_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/tools/mlangd-mla/main.mla)
set(MLANGD_MLA_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mlangd-mla)
set(MLANGD_LL_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mlangd-mla.ll)
set(MLANGD_IR_OBJECT_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mlangd-mla-ir.o)
set(MLANGD_MLA_STAMP ${CMAKE_CURRENT_BINARY_DIR}/mlangd-mla.stamp)

add_custom_command(
    OUTPUT ${MLANGD_MLA_STAMP}
    COMMAND $<TARGET_FILE:mlang> -emit-llvm ${MLANGD_MLA_SOURCE}
            --no-tests
            -Wno-unwrap
            -O0
            -o ${MLANGD_LL_OUTPUT}
    COMMAND ${MLANG_CLANGXX_EXE}
            -x ir
            -c
            ${MLANGD_LL_OUTPUT}
            -o ${MLANGD_IR_OBJECT_OUTPUT}
    COMMAND ${MLANG_CLANGXX_EXE}
            -o ${MLANGD_MLA_OUTPUT}
            ${MLANGD_IR_OBJECT_OUTPUT}
            -L${CMAKE_CURRENT_BINARY_DIR}
            -lmlang_std
            -lm
            ${OPENSSL_SSL_LIBRARY}
            ${OPENSSL_CRYPTO_LIBRARY}
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:CoreFoundation>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:CoreGraphics>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:ImageIO>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:CoreAudio>
            $<$<BOOL:${APPLE}>:-framework> $<$<BOOL:${APPLE}>:AudioToolbox>
    COMMAND ${CMAKE_COMMAND} -E touch ${MLANGD_MLA_STAMP}
    DEPENDS mlang mlang_std ${MLANGD_MLA_SOURCE}
            ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building mlangd-mla via LLVM IR + clang++ host link"
    VERBATIM
)

add_custom_target(mlangd-mla DEPENDS ${MLANGD_MLA_STAMP})

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
    # Pinned to a master commit, not v1.1.0 (Aug 2016), because v1.1.0 inherits
    # iterator types from std::iterator — deprecated in C++17 and noisily
    # diagnosed by libc++ in MacOSX26.sdk. Upstream fixed this in
    # d75bb90a "Avoid inheritance from std::iterator" (Dec 2017).
    set(MLANG_RAPIDJSON_REVISION
        "24b5e7a8b27f42fa16b96fc70aade9106cf7102f"
        CACHE STRING "RapidJSON git revision to fetch")
    message(STATUS
        "RapidJSON not found in system paths; fetching ${MLANG_RAPIDJSON_REVISION}")
    FetchContent_Declare(
        rapidjson
        GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
        GIT_TAG ${MLANG_RAPIDJSON_REVISION}
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
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT mlang-core)

option(MLANG_INSTALL_CPP_LSP
       "Install the legacy C++ mlangd target with cmake --install" OFF)
if(MLANG_INSTALL_CPP_LSP)
    install(TARGETS mlangd
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(PROGRAMS ${MLANG_FORMAT_OUTPUT}
        DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS ${MLANGD_MLA_OUTPUT}
        DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS mlang-config
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(MLANG_MANPAGE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang.1
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang-pkg.1
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlangd.1
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlangd-mla.1
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang-format.1
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang-frontend.1
    ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang-frontend-mla.1)
set(MLANG_MANPAGE_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/man/man1)

add_custom_target(manpages
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MLANG_MANPAGE_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MLANG_MANPAGE_FILES}
            ${MLANG_MANPAGE_BUILD_DIR}
    COMMENT "Staging POSIX man pages in ${MLANG_MANPAGE_BUILD_DIR}"
)

install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang.1
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang-pkg.1
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlangd.1
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlangd-mla.1
        ${CMAKE_CURRENT_SOURCE_DIR}/docs/man/mlang-format.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

install(TARGETS mlang_std
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/mlang
                COMPONENT mlang-core
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/mlang
                COMPONENT mlang-core)

# Install stdlib modules for editor tooling and go-to-definition.
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mlang/stdlib
        COMPONENT mlang-core
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
