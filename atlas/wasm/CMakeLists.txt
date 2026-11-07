# Real firmware UI core + LVGL, compiled to WebAssembly. Build with ./build.sh (uses emcmake).
cmake_minimum_required(VERSION 3.20)
project(agentlink_watch_wasm C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
# lv_conf.h is a verbatim copy of simulator/lv_conf.h (same LVGL config and version, so pixels match).
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components)

add_library(agentlink_hal STATIC web_hal.c)
target_include_directories(agentlink_hal PUBLIC ${FW}/agentlink_hal/include ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(agentlink_hal PUBLIC lvgl::lvgl)

add_subdirectory(${FW}/agentlink_proto agentlink_proto)
add_subdirectory(${FW}/core core)

add_executable(watch web_main.c)
target_link_libraries(watch PRIVATE core agentlink_hal)

set(EXPORTS
    _wasm_init _wasm_tick _wasm_pointer _wasm_button _wasm_motion _wasm_rx_buf _wasm_deliver _wasm_link_up
    _wasm_fb_ptr _wasm_take_dirty _wasm_dirty_ptr _wasm_screen_name _wasm_fps _wasm_dump_ui_state _wasm_set_wallclock)
list(JOIN EXPORTS "," EXPORTS_CSV)
target_link_options(watch PRIVATE
    -Os
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createWatchModule
    -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=8MB -sSTACK_SIZE=256KB
    -sFILESYSTEM=0 -sNO_EXIT_RUNTIME=1
    "-sEXPORTED_FUNCTIONS=${EXPORTS_CSV}"
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAP32,UTF8ToString")
target_compile_options(watch PRIVATE -Os)
target_compile_options(lvgl PRIVATE -Os)
target_compile_options(core PRIVATE -Os)
