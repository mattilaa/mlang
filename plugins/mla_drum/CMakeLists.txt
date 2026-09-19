cmake_minimum_required(VERSION 3.15.0)

project(MlaDrum
    VERSION 0.1.0
    DESCRIPTION "Mla Drum - MLang VST3 drum sampler instrument"
)

# --- Required inputs ---------------------------------------------------------
# VST3_SDK_ROOT     : a fetched Steinberg vst3sdk checkout (with submodules).
# MLANG_DSP_OBJECT  : the compiled object of src/mla_drum_dsp.mla.
# MLANG_STD_LIBRARY : the MLang runtime archive (libmlang_std.a). Defaults to
#                     the repository's build/ output; override for other trees.
if(NOT DEFINED VST3_SDK_ROOT)
    message(FATAL_ERROR "VST3_SDK_ROOT must point to the fetched vst3sdk checkout.")
endif()
if(NOT DEFINED MLANG_DSP_OBJECT)
    message(FATAL_ERROR "MLANG_DSP_OBJECT must point to the compiled MLang DSP object.")
endif()

get_filename_component(MLANG_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT DEFINED MLANG_STD_LIBRARY)
    set(MLANG_STD_LIBRARY "${MLANG_REPO_ROOT}/build/libmlang_std.a")
endif()
# Resolve to an absolute path (relative overrides are interpreted against the
# source dir, not the CMake build dir where the linker later runs).
get_filename_component(MLANG_STD_LIBRARY "${MLANG_STD_LIBRARY}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${MLANG_STD_LIBRARY}")
    message(FATAL_ERROR "MLang runtime archive not found: ${MLANG_STD_LIBRARY}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build only the plug-in target from the SDK, no examples/validator/GUI.
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES ON CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_DISABLE_CODE_SIGNING ON CACHE BOOL "" FORCE)
set(SMTG_BUILD_UNIVERSAL_BINARY OFF CACHE BOOL "" FORCE)

set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)
smtg_enable_vst3_sdk()

get_filename_component(MLANG_DSP_OBJECT_FILE "${MLANG_DSP_OBJECT}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${MLANG_DSP_OBJECT_FILE}")
    message(FATAL_ERROR "Resolved MLANG_DSP_OBJECT does not exist: ${MLANG_DSP_OBJECT_FILE}")
endif()
set_source_files_properties("${MLANG_DSP_OBJECT_FILE}"
    PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

smtg_add_vst3plugin(MlaDrum
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    src/plugin.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

# mla_sampler_protocol.h is shared with hosts (mlacker) in stdlib/include.
target_include_directories(MlaDrum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MLANG_REPO_ROOT}/stdlib/include)
target_link_libraries(MlaDrum PRIVATE sdk "${MLANG_STD_LIBRARY}")

# The WAV/AIFF decoder lives in the runtime's std::audio object, which also
# carries the CoreAudio/CoreMIDI device code; satisfy those references.
if(APPLE)
    target_link_libraries(MlaDrum PRIVATE
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox"
        "-framework AudioUnit" "-framework AVFoundation" "-framework CoreMIDI")
endif()

# The MLang runtime uses lock-free atomics; on Linux link libatomic and the
# usual runtime companions the static archive may reference.
if(NOT APPLE AND NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(MlaDrum PRIVATE atomic Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

smtg_target_set_bundle(MlaDrum
    BUNDLE_IDENTIFIER org.mlang.MlaDrum
    COMPANY_NAME MLang
)

# Offline functional tests: load the built bundle through the SDK hosting
# classes and check rendered audio. Run with `mlang pkg --config mlang.toml run test`.
option(MLA_DRUM_BUILD_TESTS "Build the Mla Drum offline functional tests" ON)
if(MLA_DRUM_BUILD_TESTS)
    if(APPLE)
        set(MLA_DRUM_MODULE_LOADER ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    elseif(WIN32)
        set(MLA_DRUM_MODULE_LOADER ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
    else()
        set(MLA_DRUM_MODULE_LOADER ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
    endif()
    add_executable(mla_drum_tests tests/mla_drum_tests.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${MLA_DRUM_MODULE_LOADER})
    target_include_directories(mla_drum_tests PRIVATE ${MLANG_REPO_ROOT}/stdlib/include)
    target_link_libraries(mla_drum_tests PRIVATE sdk_hosting)
    if(APPLE)
        set_source_files_properties(${MLA_DRUM_MODULE_LOADER} PROPERTIES COMPILE_FLAGS -fobjc-arc)
        target_link_libraries(mla_drum_tests PRIVATE "-framework Cocoa" "-framework CoreFoundation")
    else()
        target_link_libraries(mla_drum_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_dependencies(mla_drum_tests MlaDrum)
endif()
