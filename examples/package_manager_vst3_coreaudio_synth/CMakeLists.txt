cmake_minimum_required(VERSION 3.15.0)

project(MLangMiniSynth
    VERSION 0.1.0
    DESCRIPTION "MLang VST3 synth plus CoreAudio preview"
)

if(NOT APPLE)
    message(FATAL_ERROR "This example is macOS-only because the preview app uses CoreAudio.")
endif()

if(NOT DEFINED VST3_SDK_ROOT)
    message(FATAL_ERROR "VST3_SDK_ROOT must point to the fetched vst3sdk checkout.")
endif()

if(NOT DEFINED MLANG_DSP_OBJECT)
    message(FATAL_ERROR "MLANG_DSP_OBJECT must point to the compiled MLang oscillator object.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_DISABLE_CODE_SIGNING ON CACHE BOOL "" FORCE)
set(SMTG_BUILD_UNIVERSAL_BINARY OFF CACHE BOOL "" FORCE)

set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

set(MLANG_DSP_OBJECT_FILE "${MLANG_DSP_OBJECT}")
set_source_files_properties("${MLANG_DSP_OBJECT_FILE}"
    PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE
)

get_filename_component(MLANG_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(MLANG_STD_LIBRARY "${MLANG_REPO_ROOT}/build/libmlang_std.a")
if(NOT EXISTS "${MLANG_STD_LIBRARY}")
    message(FATAL_ERROR "Expected MLang runtime archive at ${MLANG_STD_LIBRARY}")
endif()

smtg_add_vst3plugin(MLangMiniSynth
    src/plug_ids.h
    src/mlang_osc_bridge.h
    src/mlang_osc_bridge.cpp
    src/plugin.h
    src/plugin.cpp
    src/plugin_entry.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

target_link_libraries(MLangMiniSynth
    PRIVATE
        sdk
        "${MLANG_STD_LIBRARY}"
)

target_include_directories(MLangMiniSynth
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

smtg_target_configure_version_file(MLangMiniSynth)
smtg_target_set_bundle(MLangMiniSynth
    BUNDLE_IDENTIFIER org.mlang.MLangMiniSynth
    COMPANY_NAME MLang
)

add_executable(mlang_coreaudio_preview
    src/coreaudio_preview.cpp
    src/mlang_osc_bridge.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

target_include_directories(mlang_coreaudio_preview
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)

target_link_libraries(mlang_coreaudio_preview
    PRIVATE
        "${MLANG_STD_LIBRARY}"
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
)
