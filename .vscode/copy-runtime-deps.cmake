cmake_minimum_required(VERSION 3.16)

foreach(required_var APP_PATH PUBLISH_DIR BUILD_DIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable: ${required_var}")
    endif()
endforeach()

file(TO_CMAKE_PATH "${APP_PATH}" APP_PATH)
file(TO_CMAKE_PATH "${PUBLISH_DIR}" PUBLISH_DIR)
file(TO_CMAKE_PATH "${BUILD_DIR}" BUILD_DIR)

if(NOT EXISTS "${APP_PATH}")
    message(FATAL_ERROR "Application binary does not exist: ${APP_PATH}")
endif()

if(NOT IS_DIRECTORY "${PUBLISH_DIR}")
    message(FATAL_ERROR "Publish directory does not exist: ${PUBLISH_DIR}")
endif()

if(NOT IS_DIRECTORY "${BUILD_DIR}")
    message(FATAL_ERROR "Build directory does not exist: ${BUILD_DIR}")
endif()

set(search_dirs
    "${PUBLISH_DIR}"
    "${BUILD_DIR}"
)

file(GET_RUNTIME_DEPENDENCIES
    RESOLVED_DEPENDENCIES_VAR resolved_deps
    UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
    EXECUTABLES "${APP_PATH}"
    DIRECTORIES ${search_dirs}
    PRE_EXCLUDE_REGEXES
        "^api-ms-win-.*"
        "^ext-ms-.*"
        "^[Aa]zure[Aa]ttest[Mm]anager\\.dll$"
        "^[Aa]zure[Aa]ttest[Nn]ormal\\.dll$"
        "^[Hh]vsi[Ff]ile[Tt]rust\\.dll$"
        "^[Pp]dm[Uu]tilities\\.dll$"
        "^wpaxholder\\.dll$"
    POST_EXCLUDE_REGEXES
        ".*/Windows/System32/.*"
        ".*/Windows/WinSxS/.*"
)

if(unresolved_deps)
    list(JOIN unresolved_deps "\n  " unresolved_text)
    message(FATAL_ERROR "Unresolved runtime dependencies:\n  ${unresolved_text}")
endif()

list(REMOVE_DUPLICATES resolved_deps)

set(copied_count 0)
foreach(dep IN LISTS resolved_deps)
    file(TO_CMAKE_PATH "${dep}" dep_path)
    get_filename_component(dep_name "${dep}" NAME)
    set(dest_path "${PUBLISH_DIR}/${dep_name}")
    if(dep_path STREQUAL dest_path)
        continue()
    endif()

    string(FIND "${dep_path}" "${BUILD_DIR}/" build_dir_prefix)
    if(build_dir_prefix EQUAL 0)
        file(COPY "${dep}" DESTINATION "${PUBLISH_DIR}")
        math(EXPR copied_count "${copied_count} + 1")
    endif()
endforeach()

message(STATUS "Resolved runtime dependencies: ${copied_count} copied")
