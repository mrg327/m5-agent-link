cmake_minimum_required(VERSION 3.20)
project(agentlink_sim C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Homebrew's sdl2-compat ships sdl2-config.cmake; hint its prefix so this works on a stock setup.
list(APPEND CMAKE_PREFIX_PATH /opt/homebrew /usr/local)
find_package(SDL2 REQUIRED)

include(FetchContent)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.5.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/components)

# The simulator's implementation of hal.h. Named `agentlink_hal` so core/ links against it exactly as it
# does against the real HAL component on the device.
add_library(agentlink_hal STATIC
    src/sim_board.c src/sim_display.c src/sim_link.c src/sim_ws.c src/sim_png.c)
target_include_directories(agentlink_hal PUBLIC ${FW}/agentlink_hal/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(agentlink_hal PUBLIC lvgl::lvgl SDL2::SDL2)

add_subdirectory(${FW}/agentlink_proto agentlink_proto)
add_subdirectory(${FW}/core core)

add_executable(agentlink_sim src/main.c src/sim_script.c)
target_link_libraries(agentlink_sim PRIVATE core agentlink_hal)
target_compile_options(agentlink_sim PRIVATE -Wall -Wextra)

enable_testing()
add_executable(test_vectors ${FW}/agentlink_proto/test/test_vectors.c)
target_link_libraries(test_vectors PRIVATE agentlink_proto)
add_test(NAME protocol_vectors
         COMMAND test_vectors ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/vectors)

add_executable(test_gesture ${FW}/core/test/test_gesture.c ${FW}/core/src/gesture.c)
target_include_directories(test_gesture PRIVATE ${FW}/core/src)
add_test(NAME gesture_recognizer COMMAND test_gesture)

add_test(NAME sim_headless_selftest
         COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/selftest.txt)
set_tests_properties(sim_headless_selftest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30)

function(sim_script_test name script)
    add_test(NAME ${name}
             COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30 ${ARGN})
endfunction()

# Double-tap on a row sends exactly the focus command and never opens the detail screen first.
sim_script_test(sim_double_tap_focus dtap_focus.txt
                PASS_REGULAR_EXPRESSION "\"ev\":\"tx_cmd\",\"id\":[0-9]+,\"op\":0,\"sid\":[0-9]+"
                FAIL_REGULAR_EXPRESSION "\"screen\":\"detail\"")
sim_script_test(sim_tap_deferred tap_deferred.txt
                FAIL_REGULAR_EXPRESSION "\"ev\":\"tx_cmd\"")
sim_script_test(sim_link_indicator link_indicator.txt)

# Link arbitration (hello on every up transport, first welcome wins) against a stubbed HAL.
add_executable(test_link_arb tests/test_link_arb.c ${FW}/core/src/link.c ${FW}/core/src/link_rx.c
               ${FW}/core/src/link_tx.c ${FW}/core/src/model.c)
target_include_directories(test_link_arb PRIVATE ${FW}/core/src)
target_link_libraries(test_link_arb PRIVATE agentlink_hal agentlink_proto lvgl::lvgl m)
add_test(NAME link_arbitration COMMAND test_link_arb)
