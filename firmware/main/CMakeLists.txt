set(AGENTLINK_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../components/core")

set(main_requires agentlink_hal ${AGENTLINK_BOARD} lvgl__lvgl)
# IDF scans requirements in a separate cmake process, so the switch is also passed via the environment.
if(NOT AGENTLINK_NO_CORE AND NOT "$ENV{AGENTLINK_NO_CORE}" AND EXISTS "${AGENTLINK_CORE_DIR}/CMakeLists.txt")
    list(APPEND main_requires core)
    set(main_defs AGENTLINK_HAVE_CORE=1)
else()
    message(STATUS "agentlink: components/core not found, building the LVGL hello fallback")
    set(main_defs AGENTLINK_HAVE_CORE=0)
endif()

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES ${main_requires}
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${main_defs})
