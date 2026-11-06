cmake_minimum_required(VERSION 3.16)

# Board BSP selection: `idf.py -DAGENTLINK_BOARD=<name> build` (boards/<name>/ must exist).
if(NOT DEFINED AGENTLINK_BOARD)
    set(AGENTLINK_BOARD "stopwatch")
endif()
set(AGENTLINK_BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/boards/${AGENTLINK_BOARD}")

# Register main, the selected board, and every project component (components/*).
file(GLOB _al_components LIST_DIRECTORIES true "${CMAKE_CURRENT_LIST_DIR}/components/*")
list(SORT _al_components)
set(COMPONENT_DIRS "$ENV{IDF_PATH}/components" "${CMAKE_CURRENT_LIST_DIR}/main" "${AGENTLINK_BOARD_DIR}")
foreach(_dir ${_al_components})
    get_filename_component(_name "${_dir}" NAME)
    if(IS_DIRECTORY "${_dir}" AND EXISTS "${_dir}/CMakeLists.txt")
        if(AGENTLINK_NO_CORE AND (_name STREQUAL "core" OR _name STREQUAL "agentlink_proto"))
            continue()
        endif()
        list(APPEND COMPONENT_DIRS "${_dir}")
    endif()
endforeach()

# `-DAGENTLINK_NO_CORE=ON` builds only the board + LVGL hello fallback (skips core, agentlink_proto).
# sdkconfig.defaults is layered with a per-board file when one exists.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${AGENTLINK_BOARD}")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.${AGENTLINK_BOARD}")
endif()

if(AGENTLINK_NO_CORE)
    set(ENV{AGENTLINK_NO_CORE} "1")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(agentlink)
