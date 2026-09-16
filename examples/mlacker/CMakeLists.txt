cmake_minimum_required(VERSION 3.15.0)

project(MlackerDrum
    VERSION 0.1.0
    DESCRIPTION "Headless mlacker drum-machine VST3 with MLang master-bus DSP"
)

if(NOT DEFINED VST3_SDK_ROOT)
    message(FATAL_ERROR "VST3_SDK_ROOT must point to the fetched vst3sdk checkout.")
endif()

if(NOT DEFINED MLANG_DSP_OBJECT)
    message(FATAL_ERROR "MLANG_DSP_OBJECT must point to the compiled MLang DSP object.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This example ships no GUI, so VSTGUI is not needed.
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

# Resolve and validate the compiled MLang DSP object.
get_filename_component(MLANG_DSP_OBJECT_FILE
    "${MLANG_DSP_OBJECT}"
    ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NOT EXISTS "${MLANG_DSP_OBJECT_FILE}")
    message(FATAL_ERROR "Resolved MLANG_DSP_OBJECT does not exist: ${MLANG_DSP_OBJECT_FILE}")
endif()
set_source_files_properties("${MLANG_DSP_OBJECT_FILE}"
    PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE
)

# Locate the MLang runtime archive built from the repository root.
get_filename_component(MLANG_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT DEFINED MLANG_STD_LIBRARY)
    find_library(MLANG_STD_LIBRARY
        NAMES mlang_std libmlang_std
        PATHS "${MLANG_REPO_ROOT}/build"
        NO_DEFAULT_PATH
    )
endif()
if(NOT MLANG_STD_LIBRARY OR NOT EXISTS "${MLANG_STD_LIBRARY}")
    message(FATAL_ERROR "Expected the MLang runtime archive under ${MLANG_REPO_ROOT}/build. "
                        "Build the compiler from the repository root first, or pass -DMLANG_STD_LIBRARY.")
endif()

set(MLACKER_SHARED_SOURCES
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    src/plug_ids.h
    src/version.h
    src/messages.h
    src/mlang_dsp_bridge.h
    src/mlang_dsp_bridge.cpp
    src/drum_engine.h
    src/drum_engine.cpp
    src/plugin.h
    src/plugin.cpp
)

# --- Target 1: the headless VST3 plug-in bundle ---------------------------
smtg_add_vst3plugin(MlackerDrum
    ${MLACKER_SHARED_SOURCES}
    src/plugin_entry.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

target_include_directories(MlackerDrum PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(MlackerDrum PRIVATE sdk "${MLANG_STD_LIBRARY}")

smtg_target_configure_version_file(MlackerDrum)
smtg_target_set_bundle(MlackerDrum
    BUNDLE_IDENTIFIER org.mlacker.MlackerDrum
    COMPANY_NAME mlacker
)

# --- Target 2: the standalone "mlacker" offline host ----------------------
add_executable(mlacker_offline_host
    ${MLACKER_SHARED_SOURCES}
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
    src/offline_host.cpp
    "${MLANG_DSP_OBJECT_FILE}"
)

target_include_directories(mlacker_offline_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(mlacker_offline_host PRIVATE sdk "${MLANG_STD_LIBRARY}")
