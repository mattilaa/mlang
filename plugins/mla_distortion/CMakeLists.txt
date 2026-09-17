cmake_minimum_required(VERSION 3.15.0)

project(MlaDistortion
    VERSION 0.1.0
    DESCRIPTION "Mla Distortion - MLang VST3 stereo saturation effect"
)

# --- Required inputs ---------------------------------------------------------
# VST3_SDK_ROOT     : a fetched Steinberg vst3sdk checkout (with submodules).
# MLANG_DSP_OBJECT  : the compiled object of src/mla_distortion_dsp.mla.
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

smtg_add_vst3plugin(MlaDistortion
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    src/plugin.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

target_include_directories(MlaDistortion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(MlaDistortion PRIVATE sdk "${MLANG_STD_LIBRARY}")

# The MLang runtime uses lock-free atomics; on Linux link libatomic and the
# usual runtime companions the static archive may reference.
if(NOT APPLE AND NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(MlaDistortion PRIVATE atomic Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

smtg_target_set_bundle(MlaDistortion
    BUNDLE_IDENTIFIER org.mlang.MlaDistortion
    COMPANY_NAME MLang
)
