# The public version is maintained only in the repository's version.txt.
set(_mlang_version_file "${CMAKE_CURRENT_LIST_DIR}/../version.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_mlang_version_file}")
file(READ "${_mlang_version_file}" _mlang_version)
string(STRIP "${_mlang_version}" _mlang_version)
if(NOT _mlang_version MATCHES
   "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "version.txt must contain MAJOR.MINOR.BUGFIX (no leading zeros)")
endif()
set(MLANG_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(MLANG_VERSION_MINOR "${CMAKE_MATCH_2}")
set(MLANG_VERSION_BUGFIX "${CMAKE_MATCH_3}")
