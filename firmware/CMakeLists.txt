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
# Boards may split into sub-components (e.g. boards/stopwatch/sw_power).
file(GLOB _al_board_subs "${AGENTLINK_BOARD_DIR}/*/CMakeLists.txt")
foreach(_cm ${_al_board_subs})
    get_filename_component(_sub "${_cm}" DIRECTORY)
    list(APPEND COMPONENT_DIRS "${_sub}")
endforeach()
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

# App images are signed (RSA-3072, no eFuses burned; FIRMWARE.md §9): an update over the air is accepted
# only if it is signed by the same key as the firmware it replaces. The key is this machine's, outside the
# repository: $AGENTLINK_FW_KEY, else ~/.agentlink/fw_signing_key.pem, made on the first build.
if(DEFINED ENV{AGENTLINK_FW_KEY})
    set(AGENTLINK_FW_KEY "$ENV{AGENTLINK_FW_KEY}")
else()
    set(AGENTLINK_FW_KEY "$ENV{HOME}/.agentlink/fw_signing_key.pem")
endif()
if(NOT EXISTS "${AGENTLINK_FW_KEY}")
    get_filename_component(_key_dir "${AGENTLINK_FW_KEY}" DIRECTORY)
    file(MAKE_DIRECTORY "${_key_dir}")
    if(DEFINED ENV{IDF_PYTHON_ENV_PATH})
        set(_py "$ENV{IDF_PYTHON_ENV_PATH}/bin/python")
    else()
        set(_py python)
    endif()
    execute_process(COMMAND "${_py}" -m espsecure generate_signing_key --version 2 --scheme rsa3072 "${AGENTLINK_FW_KEY}"
                    RESULT_VARIABLE _key_rc OUTPUT_QUIET)
    if(NOT _key_rc EQUAL 0)
        message(FATAL_ERROR "Could not create the firmware signing key ${AGENTLINK_FW_KEY}")
    endif()
    file(CHMOD "${AGENTLINK_FW_KEY}" PERMISSIONS OWNER_READ OWNER_WRITE)
    message(STATUS "Created the firmware signing key ${AGENTLINK_FW_KEY}: keep it, OTA updates need it")
endif()
# Defaults only fill a fresh sdkconfig: after changing the key path, delete firmware/sdkconfig.
file(WRITE "${CMAKE_BINARY_DIR}/sdkconfig.signing" "CONFIG_SECURE_BOOT_SIGNING_KEY=\"${AGENTLINK_FW_KEY}\"\n")
list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/sdkconfig.signing")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(agentlink)
