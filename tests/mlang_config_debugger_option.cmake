if(NOT DEFINED MLANG_CONFIG OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "MLANG_CONFIG and TEST_DIR are required")
endif()

string(RANDOM LENGTH 12 ALPHABET abcdef0123456789 suffix)
set(config_dir "${TEST_DIR}/${suffix}")
file(MAKE_DIRECTORY "${config_dir}")

function(run_config)
    execute_process(COMMAND "${MLANG_CONFIG}" --build-dir "${config_dir}" ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "mlang-config failed: ${output}${error}")
    endif()
endfunction()

function(check_preference expected)
    file(STRINGS "${config_dir}/mlang-config.conf" preference
        REGEX "^install_mladbg=")
    if(NOT preference STREQUAL "install_mladbg=${expected}")
        message(FATAL_ERROR "Expected ${expected}; saved ${preference}")
    endif()
    include("${config_dir}/mlang_config_cache.cmake")
    if(NOT "${BUILD_MLADBG}" STREQUAL "${expected}")
        message(FATAL_ERROR "CMake cache disagrees with saved preference")
    endif()
endfunction()

# Default, explicit override, and persistence across subsequent invocations.
run_config(--write)
check_preference(ON)
run_config(--mladbg off --write)
check_preference(OFF)
run_config(--write)
check_preference(OFF)
run_config(--mladbg on --write)
check_preference(ON)

# An older config has no debugger key; default to installed, preserving other
# preferences. CLI options must override the imported file in either order.
file(WRITE "${config_dir}/legacy.conf" "run_unit_tests=OFF\nrun_robot_tests=OFF\n")
run_config(--import "${config_dir}/legacy.conf" --write)
check_preference(ON)
run_config(--mladbg off --import "${config_dir}/mlang-config.conf" --write)
check_preference(OFF)

# The non-TTY bootstrap menu must expose the same toggle as the TUI.
file(WRITE "${config_dir}/menu-input" "8\ns\n")
execute_process(COMMAND "${MLANG_CONFIG}" --build-dir "${config_dir}"
    INPUT_FILE "${config_dir}/menu-input"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "Toggle mladbg installation")
    message(FATAL_ERROR "Bootstrap menu toggle failed: ${output}${error}")
endif()
check_preference(ON)
