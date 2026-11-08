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
target_include_directories(agentlink_sim PRIVATE ${FW}/core/src)
target_link_libraries(agentlink_sim PRIVATE core agentlink_hal)
target_compile_options(agentlink_sim PRIVATE -Wall -Wextra)

enable_testing()
add_executable(test_vectors ${FW}/agentlink_proto/test/test_vectors.c)
target_link_libraries(test_vectors PRIVATE agentlink_proto)
add_test(NAME protocol_vectors
         COMMAND test_vectors ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/vectors)

add_executable(test_gesture tests/test_gesture.c ${FW}/core/src/gesture.c)
target_include_directories(test_gesture PRIVATE ${FW}/core/src)
target_link_libraries(test_gesture PRIVATE m)
add_test(NAME gesture_recognizer COMMAND test_gesture)

add_test(NAME sim_headless_selftest
         COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/selftest.txt)
set_tests_properties(sim_headless_selftest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30)

function(sim_script_test name script)
    add_test(NAME ${name}
             COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30 ${ARGN})
endfunction()

sim_script_test(sim_link_indicator link_indicator.txt)
sim_script_test(sim_ring_empty ring_empty.txt)
sim_script_test(sim_ring_one one_session.txt)
sim_script_test(sim_ring_busy busy.txt)
sim_script_test(sim_ring_states ring_states.txt)
sim_script_test(sim_ring_scrub ring_scrub.txt
                PASS_REGULAR_EXPRESSION "\"ev\":\"scrub\".*\"phase\":\"start\""
                FAIL_REGULAR_EXPRESSION "\"ev\":\"tx_cmd\"|SCRIPT FAIL")
# A tap on the centre is the default action at once (no deferral); Done also acknowledges.
sim_script_test(sim_center_tap_focus center_tap_focus.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"tx_cmd\",\"id\":[0-9]+,\"op\":0")
sim_script_test(sim_approve_hold approve_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":3")
sim_script_test(sim_deny_swipe deny_swipe.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":4")
sim_script_test(sim_button_jump button_jump.txt)
# Approval safety: the card shows the request; a short or low swipe never denies; a hold before
# the card has been visible never approves.
sim_script_test(sim_approve_card approve_card.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_deny_short_swipe deny_short_swipe.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4")
add_test(NAME sim_approve_after_glow
         COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/approve_after_glow.txt)
set_tests_properties(sim_approve_after_glow PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL" PASS_REGULAR_EXPRESSION "\"op\":3")
sim_script_test(sim_approve_too_soon approve_too_soon.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")

# UI-V2 Phase 1: the HOME deck (summary row, stale/offline treatment, two-line folder names).
sim_script_test(sim_deck_summary deck_summary.txt)
sim_script_test(sim_deck_stale deck_stale.txt)
sim_script_test(sim_deck_long_names deck_long_names.txt)
sim_script_test(sim_deck_mixed deck_mixed.txt)
sim_script_test(sim_deck_ambient deck_ambient.txt)
set_tests_properties(sim_deck_ambient PROPERTIES TIMEOUT 60)
# UI-V2 Phase 2: the AGENT wheel.
sim_script_test(sim_actions_wheel actions_wheel.txt)
sim_script_test(sim_agent_timeout agent_timeout.txt)
# UI-V2 Phase 3: ASK. Question options reply by tap; grants and Deny are holds; a tap never approves;
# a rejected answer restores the options; a hold is bound to its request id.
sim_script_test(sim_ask_reply ask_reply.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":8,\"hold_ms\":0,\"arg\":1")
sim_script_test(sim_ask_grant_hold ask_grant_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":9,\"hold_ms\":0,\"arg\":1")
sim_script_test(sim_ask_grant_hold_progress ask_grant_hold_progress.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"tx_cmd\"")
sim_script_test(sim_ask_deny_hold ask_deny_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":4,\"sid\":2,\"rid\":7")
sim_script_test(sim_ask_tap_never_approves ask_tap_never_approves.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":0,\"sid\":2")
sim_script_test(sim_ask_result_error ask_result_error.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_ask_hold_bound_to_rid ask_hold_bound_to_rid.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"tx_cmd\"")

# Link arbitration (hello on every up transport, first welcome wins) against a stubbed HAL.
add_executable(test_link_arb tests/test_link_arb.c ${FW}/core/src/link.c ${FW}/core/src/link_rx.c
               ${FW}/core/src/link_tx.c ${FW}/core/src/model.c)
target_include_directories(test_link_arb PRIVATE ${FW}/core/src)
target_link_libraries(test_link_arb PRIVATE agentlink_hal agentlink_proto lvgl::lvgl m)
add_test(NAME link_arbitration COMMAND test_link_arb)
