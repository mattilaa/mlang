cmake_minimum_required(VERSION 3.10)
project(MyLanguage)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create compile_commands.json in build directory
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM package
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Find Flex and Bison
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

add_subdirectory(src)

# src/CMakeLists.txt
set(HEADER_FILES
    ${CMAKE_SOURCE_DIR}/include/AST.h
)

BISON_TARGET(MyParser parser.y ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.h)
FLEX_TARGET(MyScanner lexer.l  ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)
ADD_FLEX_BISON_DEPENDENCY(MyScanner MyParser)

add_executable(mylang
    main.cpp
    ${BISON_MyParser_OUTPUTS}
    ${FLEX_MyScanner_OUTPUTS}
    ${HEADER_FILES}
)

target_include_directories(mylang PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_include_directories(mylang PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Link against LLVM libraries
llvm_map_components_to_libnames(llvm_libs support core irreader)
target_link_libraries(mylang ${llvm_libs})
