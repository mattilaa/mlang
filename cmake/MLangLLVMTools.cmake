# Textual LLVM IR must be consumed by the same LLVM major that produced it.
# The C++ seed compiler can use a different host compiler, but its linked LLVM
# libraries determine the IR syntax used by the self-hosted tools.
string(REGEX MATCH "^[0-9]+" MLANG_LLVM_MAJOR "${LLVM_PACKAGE_VERSION}")
if(MLANG_LLVM_MAJOR STREQUAL "")
    message(FATAL_ERROR "Cannot determine LLVM major from '${LLVM_PACKAGE_VERSION}'")
endif()

find_program(MLANG_CLANGXX_EXE
    NAMES clang++-${MLANG_LLVM_MAJOR} clang++
    HINTS "${LLVM_TOOLS_BINARY_DIR}" "${LLVM_BINARY_DIR}/bin"
    NAMES_PER_DIR
    REQUIRED)
execute_process(COMMAND "${MLANG_CLANGXX_EXE}" --version
    RESULT_VARIABLE MLANG_CLANGXX_RESULT
    OUTPUT_VARIABLE MLANG_CLANGXX_VERSION
    ERROR_VARIABLE MLANG_CLANGXX_ERROR)
if(NOT MLANG_CLANGXX_RESULT EQUAL 0 OR
   NOT MLANG_CLANGXX_VERSION MATCHES "clang version ([0-9]+)")
    message(FATAL_ERROR
        "Cannot determine the LLVM version of ${MLANG_CLANGXX_EXE}: "
        "${MLANG_CLANGXX_VERSION}${MLANG_CLANGXX_ERROR}")
endif()
set(MLANG_CLANGXX_MAJOR "${CMAKE_MATCH_1}")
if(NOT MLANG_CLANGXX_MAJOR STREQUAL MLANG_LLVM_MAJOR)
    message(FATAL_ERROR
        "LLVM IR toolchain mismatch: MLang uses LLVM ${LLVM_PACKAGE_VERSION}, "
        "but ${MLANG_CLANGXX_EXE} is Clang ${MLANG_CLANGXX_MAJOR}. "
        "Install matching Clang ${MLANG_LLVM_MAJOR} and set "
        "-DMLANG_CLANGXX_EXE=/path/to/clang++, or clear the cached choice with "
        "-UMLANG_CLANGXX_EXE. To select another LLVM installation, set -DLLVM_DIR.")
endif()
message(STATUS "LLVM IR compiler: ${MLANG_CLANGXX_EXE} (LLVM ${MLANG_CLANGXX_MAJOR})")
