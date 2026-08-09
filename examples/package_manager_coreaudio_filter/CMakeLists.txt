cmake_minimum_required(VERSION 3.15)
project(MLangCoreAudioFilter LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "This example uses CoreAudio and is supported only on macOS.")
endif()
if(NOT DEFINED MLANG_FILTER_OBJECT OR NOT EXISTS "${MLANG_FILTER_OBJECT}")
    message(FATAL_ERROR "MLANG_FILTER_OBJECT must name the compiled MLang DSP object.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(MLANG_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MLANG_STD_LIBRARY "${MLANG_REPO_ROOT}/build/libmlang_std.a")
if(NOT EXISTS "${MLANG_STD_LIBRARY}")
    message(FATAL_ERROR "Build ${MLANG_STD_LIBRARY} from the repository root first.")
endif()

set_source_files_properties("${MLANG_FILTER_OBJECT}"
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

add_executable(coreaudio_filter_sweeps
    src/main.cpp
    src/filter_bridge.cpp
    "${MLANG_FILTER_OBJECT}"
)
target_include_directories(coreaudio_filter_sweeps PRIVATE src)

find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
target_link_libraries(coreaudio_filter_sweeps PRIVATE
    "${MLANG_STD_LIBRARY}"
    ${AUDIOTOOLBOX_FRAMEWORK}
    ${COREAUDIO_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
)
