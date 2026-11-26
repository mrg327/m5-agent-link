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
    src/sim_board.c src/sim_display.c src/sim_link.c src/sim_ws.c src/sim_png.c src/sim_mic.c src/sim_ota.c)
target_include_directories(agentlink_hal PUBLIC ${FW}/agentlink_hal/include ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(agentlink_hal PUBLIC lvgl::lvgl SDL2::SDL2)

add_subdirectory(${FW}/agentlink_proto agentlink_proto)
add_subdirectory(${FW}/core core)
# The gallery of the Sigil primitives (ui_kit.h, script command `kit N`) is built into the simulator only.
target_compile_definitions(core PUBLIC UI_KIT_DEMO)

add_executable(agentlink_sim src/main.c src/sim_script.c src/sim_fw_image.c)
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
# EXPERIMENTS.md "Toys": a toy never buzzes or sounds over an alert, nor where alerts would not.
add_executable(test_toy_gate tests/test_toy_gate.c ${FW}/core/src/toy_gate.c)
target_include_directories(test_toy_gate PRIVATE ${FW}/core/src)
add_test(NAME toy_gate COMMAND test_toy_gate)

add_executable(test_steps tests/test_steps.c ${FW}/core/src/steps.c)
target_include_directories(test_steps PRIVATE ${FW}/core/src)
add_test(NAME step_timeline COMMAND test_steps)

# GAUGES.md: when a usage figure may be shown, the 80 % buzz rule, the words.
add_executable(test_gauges tests/test_gauges.c ${FW}/core/src/gauges.c ${FW}/core/src/model.c ${FW}/core/src/steps.c)
target_include_directories(test_gauges PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_gauges PRIVATE m)
add_test(NAME gauges_model COMMAND test_gauges)
# DONE-SUMMARY.md: the summary line's parts, words and drop order; the model keeps and clears the summary.
add_executable(test_done tests/test_done.c ${FW}/core/src/done.c ${FW}/core/src/model.c ${FW}/core/src/steps.c ${FW}/core/src/gauges.c)
target_include_directories(test_done PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_done PRIVATE m)
add_test(NAME done_summary_model COMMAND test_done)
# ANSWER-WINDOW.md: the full text bound to its rid, in order, resumed, failing closed; the window on the device's clock.
add_executable(test_detail tests/test_detail.c ${FW}/core/src/detail.c ${FW}/core/src/model.c ${FW}/core/src/steps.c)
target_include_directories(test_detail PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_detail PRIVATE m)
add_test(NAME detail_model COMMAND test_detail)
# The request store: new or re-sent (issue 20261126-d95a); the queue's order (issue 20261126-aa28).
add_executable(test_model tests/test_model.c ${FW}/core/src/model.c ${FW}/core/src/steps.c)
target_include_directories(test_model PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_model PRIVATE m)
add_test(NAME request_model COMMAND test_model)
# GRAPH.md: the agent graph through the codec, the one-session store and the fetch schedule.
add_executable(test_graph tests/test_graph.c ${FW}/core/src/graph.c)
target_include_directories(test_graph PRIVATE ${FW}/core/src)
target_link_libraries(test_graph PRIVATE agentlink_proto)
add_test(NAME graph_model COMMAND test_graph)

# POWER.md: idle and face-down display-off, the glow, and a stamp later than the step's clock (issue 20261122-f724).
add_executable(test_power tests/test_power.c ${FW}/core/src/power.c)
target_include_directories(test_power PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_power PRIVATE lvgl::lvgl)
add_test(NAME power_timers COMMAND test_power)
# UI-V2 §5 as a check: pulse counts, the 35 ms floor, 60 ms gaps, commit buzzes told apart (issue 20261126-e39b).
add_executable(test_feedback tests/test_feedback.c ${FW}/core/src/feedback.c ${FW}/core/src/toy_gate.c)
target_include_directories(test_feedback PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_feedback PRIVATE lvgl::lvgl)
add_test(NAME feedback_haptics COMMAND test_feedback)
# Needs-you pips keep clear of the key hints at 315° and 45° (issue 20261126-54d7).
add_executable(test_ring_pip tests/test_ring_pip.c ${FW}/core/src/ui_ring.c ${FW}/core/src/ui_ring_draw.c)
target_include_directories(test_ring_pip PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_ring_pip PRIVATE agentlink_proto lvgl::lvgl m)
add_test(NAME ring_pip_clearance COMMAND test_ring_pip)

# Living marks (issue 20261126-98a6): the personality gate's guardrails; the temperament and its poses.
add_executable(test_persona tests/test_persona.c ${FW}/core/src/persona.c)
target_include_directories(test_persona PRIVATE ${FW}/core/src)
add_test(NAME persona_gate COMMAND test_persona)
add_executable(test_temper tests/test_temper.c ${FW}/core/src/temper.c ${FW}/core/src/sha256.c)
target_include_directories(test_temper PRIVATE ${FW}/core/src)
target_link_libraries(test_temper PRIVATE m)
add_test(NAME temper_poses COMMAND test_temper)
# While you were away (issue 20261126-9f99): the ring's last hour, the away detector, the 2 s clock.
add_executable(test_replay tests/test_replay.c ${FW}/core/src/replay.c)
target_include_directories(test_replay PRIVATE ${FW}/core/src)
add_test(NAME replay_hour COMMAND test_replay)
# The Mote (issue 20261126-59e0): the light's modes, speeds, tail, night, level by green days.
add_executable(test_mote tests/test_mote.c ${FW}/core/src/mote.c)
target_include_directories(test_mote PRIVATE ${FW}/core/src)
target_link_libraries(test_mote PRIVATE m)
add_test(NAME mote_modes COMMAND test_mote)
# Easter eggs (issue 20261126-7770): the clock slip's minutes and frames, the centuries, the spin.
add_executable(test_eggs tests/test_eggs.c ${FW}/core/src/eggs.c)
target_include_directories(test_eggs PRIVATE ${FW}/core/src)
target_link_libraries(test_eggs PRIVATE m)
add_test(NAME eggs_triggers COMMAND test_eggs)

add_executable(test_ring tests/test_ring.c ${FW}/core/src/ring.c)
target_include_directories(test_ring PRIVATE ${FW}/core/src)
find_package(Threads REQUIRED)
target_link_libraries(test_ring PRIVATE Threads::Threads)
add_test(NAME ring_fifo COMMAND test_ring)

add_executable(test_sha256 tests/test_sha256.c ${FW}/core/src/sha256.c)
target_include_directories(test_sha256 PRIVATE ${FW}/core/src)
add_test(NAME sha256 COMMAND test_sha256)
add_executable(test_nav tests/test_nav.c ${FW}/core/src/nav.c)
target_include_directories(test_nav PRIVATE ${FW}/core/src)
add_test(NAME nav_model COMMAND test_nav)
# Reminders that grow louder (issue 20261126-a909): the schedule, one ladder, the floor, the tail.
add_executable(test_nag tests/test_nag.c ${FW}/core/src/nag.c)
target_include_directories(test_nag PRIVATE ${FW}/core/src)
add_test(NAME nag_ladder COMMAND test_nag)

add_executable(test_sound tests/test_sound.c ${FW}/core/src/sound.c ${FW}/core/src/sound_bank.c)
target_include_directories(test_sound PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_sound PRIVATE lvgl::lvgl m)
add_test(NAME sound_render COMMAND test_sound)

# Equal-advance digits in every UI font (UI-V3 2.1): needs only the font tables and LVGL's text measurement.
add_executable(test_fonts tests/test_fonts.c ${FW}/core/src/fonts/font_figtree_20_400.c
               ${FW}/core/src/fonts/font_figtree_20_500.c ${FW}/core/src/fonts/font_figtree_20_600.c
               ${FW}/core/src/fonts/font_figtree_24_600.c ${FW}/core/src/fonts/font_figtree_34_600.c
               ${FW}/core/src/fonts/font_figtree_56_600.c ${FW}/core/src/fonts/font_phosphor_20.c
               ${FW}/core/src/fonts/font_phosphor_28.c ${FW}/core/src/fonts/font_phosphor_44.c)
target_include_directories(test_fonts PRIVATE ${FW}/core/src)
target_link_libraries(test_fonts PRIVATE lvgl::lvgl m)
add_test(NAME tabular_digits COMMAND test_fonts)

add_test(NAME sim_headless_selftest
         COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/selftest.txt)
set_tests_properties(sim_headless_selftest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30)

function(sim_script_test name script)
    add_test(NAME ${name}
             COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30 ${ARGN})
endfunction()

sim_script_test(sim_link_indicator link_indicator.txt)
# USB is preferred when plugged in (over a BLE link already welcomed): hello on USB at once and again
# after 5 s, the USB welcome takes over, unplugging USB goes back to a BLE handshake.
sim_script_test(sim_link_prefer_usb link_prefer_usb.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"ev\":\"welcome\".*\"fake_tx\",\"transport\":0.*\"fake_tx\",\"transport\":0.*\"ev\":\"prefer_usb\",\"from\":1.*\"ev\":\"welcome\".*\"ev\":\"down\",\"transport\":0.*\"fake_tx\",\"transport\":1")
# Two BLE centrals (MULTI-LINK.md phase 1): the second waits, unheard, and takes the link when the first drops.
sim_script_test(sim_link_two_ble link_two_ble.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"ev\":\"welcome\".*\"ev\":\"up\",\"transport\":3.*\"ev\":\"down\",\"transport\":1.*\"fake_tx\",\"transport\":3.*\"ev\":\"welcome\"")
sim_script_test(sim_ring_empty ring_empty.txt)
sim_script_test(sim_strings_clip strings_clip.txt)
sim_script_test(sim_ring_one one_session.txt)
sim_script_test(sim_ring_busy busy.txt)
sim_script_test(sim_ring_states ring_states.txt)
sim_script_test(sim_ring_pips ring_pips.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_hint_tap_home hint_tap_home.txt
                FAIL_REGULAR_EXPRESSION "\"via\":\"tap\"|SCRIPT FAIL")
sim_script_test(sim_hint_badge hint_badge.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
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
sim_script_test(sim_deny_swipe deny_swipe.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4")
sim_script_test(sim_button_jump button_jump.txt)
# Bench (BENCH.md): daemon strokes play as a finger; the frame timeline comes back in dbg_frames.
sim_script_test(sim_bench_stroke bench_stroke.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"bench_dump\"")
sim_script_test(sim_bench_swipe bench_swipe.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"cat\":\"input\",\"ev\":\"swipe_right\"")
# Issue 20261123-435a: every played stroke is read (reads > 0) and ends; touch_unread would mean reads stopped.
sim_script_test(sim_bench_stroke_read bench_stroke.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|touch_unread|stroke_end\",\"reads\":0,"
                PASS_REGULAR_EXPRESSION "\"ev\":\"stroke_end\",\"reads\":[1-9]")
# The bench's task scripts (bench/tasks, BENCH.md §5) play in the simulator too, each ending with its command.
function(bench_task_test name pass)
    add_test(NAME sim_task_${name}
             COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/../bench/tasks/${name}.txt)
    set_tests_properties(sim_task_${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                         FAIL_REGULAR_EXPRESSION "SCRIPT FAIL" PASS_REGULAR_EXPRESSION "${pass}")
endfunction()
bench_task_test(approve "\"op\":3,\"sid\":2,\"rid\":7")
bench_task_test(answer "\"op\":5,\"sid\":2,\"rid\":8,\"hold_ms\":0,\"arg\":1")
bench_task_test(reply "\"op\":11,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":100")
bench_task_test(switch "\"op\":0,\"sid\":2")
bench_task_test(deny "\"op\":4,\"sid\":2,\"rid\":7")
# Screens with no command at the end (UX profile, issue 20261122-761f): the script's own expect_screen lines are the check.
foreach(t agent_back watch status labs busy arrival)
    add_test(NAME sim_task_${t}
             COMMAND agentlink_sim --headless --no-ws --script ${CMAKE_CURRENT_SOURCE_DIR}/../bench/tasks/${t}.txt)
    set_tests_properties(sim_task_${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                         FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
endforeach()
# Approval safety: the card shows the request; a short or low swipe never denies; a hold before
# the card has been visible never approves.
sim_script_test(sim_approve_card approve_card.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_deny_short_swipe deny_short_swipe.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4")
add_test(NAME sim_approve_after_glow
         COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/approve_after_glow.txt)
set_tests_properties(sim_approve_after_glow PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL" PASS_REGULAR_EXPRESSION "\"op\":3")
sim_script_test(sim_approve_too_soon approve_too_soon.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
# Tap to wake: the contact that wakes a dark display is spent on waking, so it can never approve.
add_test(NAME sim_tap_wake_hold
         COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tap_wake_hold.txt)
set_tests_properties(sim_tap_wake_hold PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"display_off\".*\"wake_input\",\"touch\":true")
add_test(NAME sim_tap_wake_tap
         COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tap_wake_tap.txt)
set_tests_properties(sim_tap_wake_tap PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"display_off\".*\"wake_input\",\"touch\":true")

# Power: DEEP only on battery with no link, counted from display-off; timer wakes stay dark and
# silent. Timer wakes are off (POWER_TIMER_WAKES=0), so every DEEP arms buttons only (wake_after_s 0).
function(sim_power_test name script)
    cmake_parse_arguments(P "" "" "ARGS;PROPS" ${ARGN})
    add_test(NAME ${name} COMMAND agentlink_sim --headless --no-ws ${P_ARGS}
             --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script})
    set_tests_properties(${name} PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30 ${P_PROPS})
endfunction()
sim_power_test(sim_deep_battery deep_sleep.txt ARGS --battery --idle-off 1 --deep-after 2
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"display_on\".*\"ev\":\"deep\",\"wake_after_s\":0,\"tries\":0")
sim_power_test(sim_deep_not_on_usb deep_wait.txt ARGS --idle-off 1 --deep-after 1
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"deep\"")
# BLE pairing (PROTOCOL.md §8): the passkey wakes a dark display and shows until done or fail.
sim_power_test(sim_ble_pair ble_pair.txt ARGS --idle-off 1
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|042917|551200"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"display_off\".*\"ev\":\"display_on\".*\"shown\":true.*\"shown\":false.*\"text\":\"Paired\".*\"shown\":true.*\"shown\":false.*\"text\":\"Pairing failed\"")
# Desk mode: the default 60 s timeout (--idle-off 60) becomes 10 minutes on USB, stays 60 s on battery.
sim_power_test(sim_desk_usb_stays_on desk_idle.txt ARGS --idle-off 60
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"display_off\"")
sim_power_test(sim_desk_battery_off desk_idle.txt ARGS --battery --idle-off 60
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"display_off\"")
sim_power_test(sim_deep_not_linked deep_linked.txt ARGS --battery --idle-off 1 --deep-after 1
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"deep\"")
sim_power_test(sim_deep_timer_wake deep_wait.txt ARGS --battery --deep-after 2 --wake timer --tries 2
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"haptic\"|\"ev\":\"display_on\""
                     PASS_REGULAR_EXPRESSION "\"ev\":\"deep\",\"wake_after_s\":0,\"tries\":2")
sim_power_test(sim_deep_ladder_end deep_wait.txt ARGS --battery --deep-after 2 --wake timer --tries 7
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"deep\",\"wake_after_s\":0,\"tries\":7")

# UI-V2 Phase 1: the HOME deck (summary row, stale/offline treatment, two-line folder names).
sim_script_test(sim_deck_summary deck_summary.txt)
sim_script_test(sim_deck_stale deck_stale.txt)
sim_script_test(sim_deck_long_names deck_long_names.txt)
sim_script_test(sim_deck_mixed deck_mixed.txt)
sim_script_test(sim_remote_host remote_host.txt)
sim_script_test(sim_deck_ambient deck_ambient.txt)
set_tests_properties(sim_deck_ambient PROPERTIES TIMEOUT 60)
# UI-V2 Phase 2: the AGENT wheel.
sim_script_test(sim_actions_wheel actions_wheel.txt)
sim_script_test(sim_agent_timeout agent_timeout.txt)
# Experiments (issue 61ce): LABS and the Trackpad; a blocked item never opens, clicks and wheel go out as op 20.
sim_script_test(sim_labs_trackpad labs_trackpad.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"wheel\":-"
                PASS_REGULAR_EXPRESSION "\"btn\":0,\"n\":2.*\"btn\":1,\"n\":1.*\"wheel\":1")
# Living marks (issue 20261126-98a6): the breath on the dock and its stillness on battery, with reduced motion,
# off and while needed; the one-shots of done, error and snooze; LABS > Personality and the stored word.
sim_script_test(sim_fun_temper fun_temper.txt)
sim_script_test(sim_fun_temper_states fun_temper_states.txt)
# While you were away (issue 20261126-9f99): the first look after 15 min dark replays the hour on the
# bezel, and a key ends it at once; a waiting request, reduced motion or the switch off skip it.
add_test(NAME sim_fun_replay
         COMMAND agentlink_sim --headless --no-ws --idle-off 61 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fun_replay.txt)
set_tests_properties(sim_fun_replay PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]"
                     PASS_REGULAR_EXPRESSION "\"replay\",\"act\":\"start\".*\"why\":\"done\".*\"act\":\"start\".*\"why\":\"input\"")
add_test(NAME sim_fun_replay_blocked
         COMMAND agentlink_sim --headless --no-ws --idle-off 61 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fun_replay_blocked.txt)
set_tests_properties(sim_fun_replay_blocked PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"act\":\"start\""
                     PASS_REGULAR_EXPRESSION "\"why\":\"gate\".*\"why\":\"gate\".*\"why\":\"still\"")
sim_script_test(sim_labs_fun labs_fun.txt)
# The Mote (issue 20261126-59e0): lap, a big green finish (the day counted, a cheer), settle, ember, tired,
# night; gone on battery and at once for a request.
sim_script_test(sim_fun_mote fun_mote.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"mode\":\"lap\".*\"mote_day\",\"days\":1.*\"mode\":\"cheer\".*\"mode\":\"settle\".*\"mode\":\"ember\".*\"mode\":\"gone\".*\"mode\":\"lap\".*\"mode\":\"gone\"")
# Easter eggs (issue 20261126-7770): the clock slip at 13:37 (once) and 04:04; the 100th finish spins the sigil,
# held while a request waits and played after it; the 1000th loops the Mote.
sim_script_test(sim_fun_eggs fun_eggs.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"hm\":1337.*\"hm\":1337"
                PASS_REGULAR_EXPRESSION "\"hm\":1337.*\"hm\":404.*\"egg\":\"century\",\"n\":100,\"by\":\"sigil\".*\"ev\":\"century\",\"n\":100.*\"ev\":\"state\",\"screen\":\"ask\".*\"egg\":\"century\",\"n\":100,\"by\":\"sigil\".*\"egg\":\"century\",\"n\":1000,\"by\":\"mote\".*\"mode\":\"loop\"")
# UI-V2 Phase 3: ASK. Question options reply by tap; grants and Deny are holds; a tap never approves;
# a rejected answer restores the options; a hold is bound to its request id.
sim_script_test(sim_ask_reply ask_reply.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":8,\"hold_ms\":0,\"arg\":1.*\"pattern_ms\":\\[35,80,120\\]")
sim_script_test(sim_ask_grant_hold ask_grant_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":9,\"hold_ms\":1[0-9][0-9][0-9],\"arg\":1,([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[150\\]")
# A permission option that refuses buzzes like Deny, a grant option like Approve (issue 20261126-e39b).
sim_script_test(sim_ask_deny_option_haptic ask_deny_option_haptic.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":9,\"hold_ms\":[0-9]+,\"arg\":2,([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[60,90,60\\]")
sim_script_test(sim_ask_grant_hold_progress ask_grant_hold_progress.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"tx_cmd\"")
sim_script_test(sim_ask_deny_hold ask_deny_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":4,\"sid\":2,\"rid\":7")
sim_script_test(sim_ask_tap_never_approves ask_tap_never_approves.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":0,\"sid\":2")
# UX-PROFILE C3-C5 (issue 67d1): an option's glyph from its mask bit and its label, an unclear
# option or a blank request never answered from the watch, a long command fills its lines, and a
# 33rd session's request still shows.
sim_script_test(sim_ask_option_glyphs ask_option_glyphs.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3,|\"op\":4,|\"op\":5,")
sim_script_test(sim_ask_blank_request ask_blank_request.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3,|\"op\":4,|\"op\":5,")
sim_script_test(sim_ask_long_command ask_long_command.txt)
sim_script_test(sim_content_session_pool_full content_session_pool_full.txt)
# UX-PROFILE C7-C8 (issue 4094): an overdue job, a one-word message, reader pages and code blocks.
sim_script_test(sim_content_job_overdue content_job_overdue.txt)
sim_script_test(sim_content_message_word content_message_word.txt)
sim_script_test(sim_content_reader_blocks content_reader_blocks.txt)
# UI-V3 §3.3: tapping any ASK row (selected or neighbour) never approves, denies or answers a hold item;
# tapping a neighbour question option sends that option's reply.
sim_script_test(sim_ask_tap_rows ask_tap_rows.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_ask_tap_option_row ask_tap_option_row.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":8,\"hold_ms\":0,\"arg\":1.*\"pattern_ms\":\\[35,80,120\\]")
# UI-V3: swipe right goes back from AGENT, STATUS, VOICE, WATCH, SCROLL and ASK; in ASK it never answers
# (no approve, deny or reply op), the request stays and a tap reopens it.
sim_script_test(sim_back_swipe_right back_swipe_right.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"mode\":\"agent\",\"gesture\":\"swipe_right\".*\"mode\":\"status\",\"gesture\":\"swipe_right\".*\"mode\":\"voice\",\"gesture\":\"swipe_right\".*\"mode\":\"watch\",\"gesture\":\"swipe_right\".*\"mode\":\"scroll\",\"gesture\":\"swipe_right\".*\"mode\":\"ask\",\"gesture\":\"swipe_right\",\"action\":\"back\"")
set_tests_properties(sim_back_swipe_right PROPERTIES TIMEOUT 60)
sim_script_test(sim_ask_result_error ask_result_error.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_ask_hold_bound_to_rid ask_hold_bound_to_rid.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"tx_cmd\"")
# A welcome (a new link, maybe a restarted daemon) drops every card: rids may begin again (issue 1173).
sim_script_test(sim_ask_cleared_on_new_link ask_cleared_on_new_link.txt
  PASS_REGULAR_EXPRESSION "attention_clear_all" FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# SCROLL, entered by a drag on WATCH (issue 5cbb; Phase 4 trackpad). A drag is lines (op 10, unit 0),
# the first sent with its step; a flick is never a page (unit 1 only from B); A to_end (unit 2) and back
# to WATCH; leaving by tap, B long or timeout never sends to_end; privacy meta hides the preview;
# `unsupported` closes SCROLL and later drags only toast.
sim_script_test(sim_scroll_mode scroll_mode.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"unit\":1,\"delta\":-"
                PASS_REGULAR_EXPRESSION "\"action\":\"scroll_drag\"}[\r\n]+{[^{}]*\"ev\":\"tx_scroll\",\"unit\":0,\"delta\":-1,\"seq\":[0-9]+}.*\"ev\":\"tx_scroll\",\"unit\":1,\"delta\":1,.*\"ev\":\"tx_scroll\",\"unit\":2,\"delta\":1,")
# Trackpad glide: a fast release keeps sending lines; a touch stops it, and the tap that caught it stays.
sim_script_test(sim_scroll_momentum scroll_momentum.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"action\":\"back\"}.*\"ev\":\"tx_scroll\""
                PASS_REGULAR_EXPRESSION "\"action\":\"scroll_fling\"}.*\"ev\":\"scroll_glide\",\"v\":-[0-9]+}.*\"ev\":\"tx_scroll\",\"unit\":0,\"delta\":-[0-9]+,.*\"ev\":\"tx_scroll\",\"unit\":0,\"delta\":-[0-9]+,.*\"ev\":\"tx_scroll\",\"unit\":0,\"delta\":-[0-9]+,.*\"action\":\"back\"}")
sim_script_test(sim_scroll_exit scroll_exit.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"unit\":2")
set_tests_properties(sim_scroll_exit PROPERTIES TIMEOUT 60)
sim_script_test(sim_scroll_privacy scroll_privacy.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_scroll_unsupported scroll_unsupported.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# The Reply list follows the agent's closing question: the session's answers first ("No" = type arg 101).
sim_script_test(sim_reply_question reply_question.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"op\":11,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":101")
# Voice dictation (issue 0ea9): Dictate... records, streams ADPCM blocks, shows the transcript; a tap
# never sends; the hold sends op 12 with hold_ms >= 1000.
sim_script_test(sim_voice_dictate voice_dictate.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":12,\"sid\":[0-9]+,\"rid\":0,\"hold_ms\":[0-9][0-9]?[0-9]?,"
  PASS_REGULAR_EXPRESSION "\"msg\":\"audio_start\".*\"msg\":\"audio_end\".*\"ev\":\"stop\",\"aid\":[0-9]+,\"ms\":[0-9]+,\"blocks\":[1-9].*\"op\":12,\"sid\":1,\"rid\":0,\"hold_ms\":1[0-9][0-9][0-9],\"arg\":[0-9]+")
# Dictating an answer (issue 7941): a free-text question offers Dictate... before Open on PC; the hold in
# VOICE sends op 12 with the request's rid; ASK shows it pending until the daemon's clear.
sim_script_test(sim_ask_dictate ask_dictate.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":12,\"sid\":[0-9]+,\"rid\":0,"
  PASS_REGULAR_EXPRESSION "\"msg\":\"audio_start\".*\"op\":12,\"sid\":2,\"rid\":8,\"hold_ms\":1[0-9][0-9][0-9],\"arg\":[0-9]+")
# KEYS swipe keyboard (issue 1e57): kb_start on open, swipes and taps as kb_stroke with counting seq, the
# layer key resends kb_start with the same kid, B deletes (op 14), only the A hold sends (op 15,
# hold_ms >= 1000), B held leaves with kb_end arg 1 (op 16).
sim_script_test(sim_keys_basic keys_basic.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":15,\"sid\":[0-9]+,\"rid\":0,\"hold_ms\":[0-9][0-9]?[0-9]?,"
  PASS_REGULAR_EXPRESSION "\"msg\":\"kb_start\".*\"seq\":0,.*\"seq\":1,.*\"msg\":\"kb_start\".*\"msg\":\"kb_start\".*\"op\":14,\"sid\":1.*\"op\":15,\"sid\":1,\"rid\":0,\"hold_ms\":1[0-9][0-9][0-9].*\"op\":16,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1")
# KEYS: a double tap below the keys leaves with kb_end arg 1; a single tap there is never a stroke.
sim_script_test(sim_keys_back keys_back.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"stroke\""
  PASS_REGULAR_EXPRESSION "\"ev\":\"back\".*\"op\":16,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1")
# KEYS candidates: a tap on a candidate is kb_pick (op 13) with its index, never a stroke; the chord
# leaves with kb_end arg 1.
sim_script_test(sim_keys_pick keys_pick.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"seq\":1,"
  PASS_REGULAR_EXPRESSION "\"op\":13,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1.*\"arg\":2.*\"arg\":0.*\"op\":16,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1")
# KEYS refusals show as short reasons; an arriving request waits until the user leaves (kb_end arg 1),
# then opens ASK.
sim_script_test(sim_keys_refusal keys_refusal.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"op\":16,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1.*\"mode\":\"home\".*\"mode\":\"ask\"")
# How a request takes over the watch (issues 883f, 4b25, 09b8, 5f79): a request never closes KEYS
# or STATUS, it waits (a dot) and opens on the way back; a late refusal brings its request back; a
# finger already down when ASK opens is swallowed; a wake after an unseen glow opens the request.
sim_script_test(sim_takeover_keys_waits takeover_keys_waits.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_takeover_status_waits takeover_status_waits.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# The session READ is open on ends: the deck says "Session ended" (9df4, UX-PROFILE E7).
sim_script_test(sim_takeover_session_ended takeover_session_ended.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# A second request that arrives in ASK opens once the first is answered (9df4, UX-PROFILE E5).
sim_script_test(sim_takeover_second_request takeover_second_request.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# AGENT keeps its selected item across a state change of the session (9df4, UX-PROFILE E9).
sim_script_test(sim_takeover_agent_keeps_item takeover_agent_keeps_item.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_takeover_late_err takeover_late_err.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"result\",\"ok\":false,\"err\":0,\"op\":3.*\"mode\":\"ask\".*\"text\":\"Out of date\"")
# Requests stop yanking open screens (issue 20261126-0a74): AGENT and SCROLL keep the work and show the
# badge, opening the request when they close themselves; on the deck a request waits until the user has
# left it alone 1.5 s; a tap on the badge goes to the most urgent request and answers nothing.
sim_script_test(sim_takeover_scroll_waits takeover_scroll_waits.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]")
sim_script_test(sim_takeover_agent_waits takeover_agent_waits.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[3456]")
sim_script_test(sim_takeover_home_settle takeover_home_settle.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"settling\"|\"op\":[345]"
  PASS_REGULAR_EXPRESSION "\"mode\":\"home\",\"gesture\":\"btn_b\".*\"ev\":\"wait\",\"sid\":3.*\"mode\":\"home\",\"gesture\":\"btn_b\".*\"queue_next\",\"rid\":8.*\"mode\":\"ask\"")
sim_script_test(sim_takeover_badge_tap takeover_badge_tap.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]"
  PASS_REGULAR_EXPRESSION "\"badge_tap\",\"rid\":8.*\"mode\":\"ask\"")
# Only a new rid alerts (issue 20261126-d95a): a resync's and a reconnect's re-send of the open request
# gives one arrival pattern in all, and ASK comes back after the welcome; the new rid 8 still buzzes.
sim_script_test(sim_seen_resend_silent seen_resend_silent.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|pattern_ms\":\\[250,120,250\\].*pattern_ms\":\\[250,120,250\\].*\"rid\":8,\"sid\":3"
  PASS_REGULAR_EXPRESSION "\"ev\":\"resent\".*\"ev\":\"ask_restored\".*\"rid\":8,\"sid\":3,\"kind\":0,\"new\":true.*pattern_ms\":\\[250,120,250\\]")
# The request queue (issue 20261126-aa28): every pending request opens in turn, least time left first;
# one the user went back from is skipped; none opens under a finger, and that finger approves nothing.
sim_script_test(sim_queue_three queue_three.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# The next request slides in after an answer (issue 20261126-74c0): from the right, with a tick, no quiet
# wait; the badge counts the others. Exactly one approve per hold.
sim_script_test(sim_queue_slide queue_slide.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3,\"sid\":[0-9]+,\"rid\":7.*\"op\":3,\"sid\":[0-9]+,\"rid\":7"
  PASS_REGULAR_EXPRESSION "\"op\":3,\"sid\":2,\"rid\":7.*\"slide_in\",\"rid\":9.*\"op\":3,\"sid\":4,\"rid\":9.*\"slide_in\",\"rid\":8.*\"op\":3,\"sid\":3,\"rid\":8")
sim_script_test(sim_queue_skip_back queue_skip_back.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
sim_script_test(sim_queue_finger_down queue_finger_down.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3,\"sid\":3"
  PASS_REGULAR_EXPRESSION "\"op\":3,\"sid\":2,\"rid\":7.*\"queue_next\",\"rid\":8")
# A takeover's screen settles (issue 20261126-86cd): a tap or button A begun before, or just after, a
# request opens ASK does not act on it; a deliberate one after TAKEOVER_SETTLE_MS does at once.
sim_script_test(sim_takeover_settle_tap takeover_settle_tap.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"settling\",\"gesture\":\"tap\".*\"mode\":\"ask\",\"gesture\":\"tap\",\"action\":\"commit\"")
sim_script_test(sim_takeover_settle_button takeover_settle_button.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0,.*\"op\":0,"
  PASS_REGULAR_EXPRESSION "\"settling\",\"gesture\":\"btn_a\".*\"op\":0,\"sid\":3,\"rid\":8")
add_test(NAME sim_takeover_glow_missed
         COMMAND agentlink_sim --headless --no-ws --idle-off 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/takeover_glow_missed.txt)
set_tests_properties(sim_takeover_glow_missed PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"glow\".*\"display_off\".*\"wake_input\",\"touch\":true")
# The glow says who and what (issue 20261126-0ca0): the sigil and the kind's glyph for 3 s, one glow
# per alert, and a reminder lights the dark display again; the touch after it only reveals.
add_test(NAME sim_glow_who_what
         COMMAND agentlink_sim --headless --no-ws --idle-off 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/glow_who_what.txt)
set_tests_properties(sim_glow_who_what PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]"
                     PASS_REGULAR_EXPRESSION "\"glow\",\"rgb\":[0-9]+,\"sid\":2.*\"display_off\".*\"sid\":3.*\"display_off\".*\"sid\":4.*\"display_off\".*\"glow\",\"rgb\":[0-9]+,\"sid\":2.*\"wake_input\",\"touch\":true")
# Reminders grow louder (issue 20261126-a909): on a dark display the soft pulse leaves it dark, the
# second and third light the glow; a question is reminded with its own pattern; one ladder at a time,
# for the request with the least time left, then the next one's, 10 s apart at least.
add_test(NAME sim_nag_glow_dark
         COMMAND agentlink_sim --headless --no-ws --idle-off 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/nag_glow_dark.txt)
set_tests_properties(sim_nag_glow_dark PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]|\"level\":4"
                     PASS_REGULAR_EXPRESSION "\"level\":1}([^g]|g[^l]|gl[^o])*\"level\":2.*pattern_ms\":\\[250,120,250\\].*\"ev\":\"glow\".*\"level\":3.*pattern_ms\":\\[250,120,250,400,250,120,250\\].*\"ev\":\"sound\".*\"ev\":\"glow\"")
add_test(NAME sim_nag_question
         COMMAND agentlink_sim --headless --no-ws --idle-off 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/nag_question.txt)
set_tests_properties(sim_nag_question PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]"
                     PASS_REGULAR_EXPRESSION "\"nag\",\"rid\":8,\"level\":1.*pattern_ms\":\\[60\\].*\"level\":2.*pattern_ms\":\\[40,90,40\\].*\"level\":3.*pattern_ms\":\\[40,90,40,400,40,90,40\\]")
add_test(NAME sim_nag_two_requests
         COMMAND agentlink_sim --headless --no-ws --idle-off 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/nag_two_requests.txt)
set_tests_properties(sim_nag_two_requests PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"nag\",\"rid\":7.*\"nag\",\"rid\":8|\"nag\",\"rid\":7,\"level\":1"
                     PASS_REGULAR_EXPRESSION "\"nag\",\"rid\":8,\"level\":1.*\"nag\",\"rid\":8,\"level\":2.*\"nag\",\"rid\":8,\"level\":3.*\"nag\",\"rid\":7,\"level\":2")
add_test(NAME sim_nag_wake_stops
         COMMAND agentlink_sim --headless --no-ws --idle-off 3 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/nag_wake_stops.txt)
set_tests_properties(sim_nag_wake_stops PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]|\"level\":2"
                     PASS_REGULAR_EXPRESSION "\"nag\",\"rid\":7,\"level\":1.*\"wake_input\".*\"display_off\"")
# Quick replies and interrupt (issue b150): reply 1 goes out as op 11 with arg 1; a tap on Interrupt
# sends nothing; the hold sends op 6 with hold_ms >= 1000.
sim_script_test(sim_quick_replies quick_replies.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,\"sid\":[0-9]+,\"rid\":0,\"hold_ms\":[0-9][0-9]?[0-9]?,"
  PASS_REGULAR_EXPRESSION "\"op\":11,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1.*\"op\":6,\"sid\":2,\"rid\":0,\"hold_ms\":1[0-9][0-9][0-9],")
# A tap on a list row acts on that row (UI-V3 §3.3): a safe neighbour commits (op 8), Stop only selects.
sim_script_test(sim_agent_row_tap agent_row_tap.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,"
  PASS_REGULAR_EXPRESSION "\"op\":8,\"sid\":1,")
# UI-V2 Phase 5: STATUS. Swipe down from HOME (never deny there), the rows, the quiet cycle and every
# exit path; quiet mode holds the permission buzz until it is turned off again, then it buzzes.
# Volume: each step sets the speaker and plays the preview there; a step while muted unmutes
sim_script_test(sim_status status.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"volume_set\",\"step\":1,\"unmuted\":false.*\"ev\":\"volume\",\"percent\":10.*\"ev\":\"sound\".*\"volume_set\",\"step\":3,\"unmuted\":true")
set_tests_properties(sim_status PROPERTIES TIMEOUT 120)
sim_script_test(sim_status_quiet_no_clock status_quiet_no_clock.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"quiet_set\",\"step\":[12]")
sim_script_test(sim_quiet_buzz quiet_buzz.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4|\"ev\":\"glow\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"quiet\",\"rid\":7,.*\"ev\":\"quiet_set\",\"step\":0,\"until\":0.*\"ev\":\"attention\",\"rid\":8.*\"pattern_ms\":\\[250,120,250\\]")

# Protocol 0.3: AGENT item lists and toggles, the WATCH screen, the finished buzz and snoozed attentions.
sim_script_test(sim_agent_items agent_items.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_agent_items PROPERTIES TIMEOUT 60)
sim_script_test(sim_watch watch.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_watch PROPERTIES TIMEOUT 60)
sim_script_test(sim_watch_ask_return watch_ask_return.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# EXPERIMENTS.md "Toys" (issue 20261126-b5d1): raw keys, the toy gate, yield to a request and return.
sim_script_test(sim_toy_yield toy_yield.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_watch_ask_return PROPERTIES TIMEOUT 60)
sim_script_test(sim_watch_states watch_states.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_watch_states PROPERTIES TIMEOUT 60)
# Sigil (GLYPHS.md §5 WATCH): the lane of glyph nodes in every state, the report's words, the comet's redraw budget.
# A tap on a node selects that step: it must never focus the terminal (op 0) nor answer anything (op 3, op 4).
sim_script_test(sim_sigil_watch sigil_watch.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0,|\"op\":3|\"op\":4")
set_tests_properties(sim_sigil_watch PROPERTIES TIMEOUT 90)
# UI-V3 second safety pass: a hold acts on the row under the finger; early taps after opening are ignored;
# a moved selection cancels a hold; the answered state lingers; a long transcript cannot be sent.
sim_script_test(sim_ask_hold_neighbour_row ask_hold_neighbour_row.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_agent_hold_neighbour_row agent_hold_neighbour_row.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,|\"op\":8,")
# Legibility pass: the selected hold row is 73 px (7 mm) on ASK and AGENT; a hold starts only on that row.
sim_script_test(sim_ask_tall_row_in ask_tall_row_in.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL" PASS_REGULAR_EXPRESSION "\"op\":3")
sim_script_test(sim_ask_tall_row_out ask_tall_row_out.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_agent_tall_row_in agent_tall_row_in.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL" PASS_REGULAR_EXPRESSION "\"op\":6,")
sim_script_test(sim_agent_tall_row_out agent_tall_row_out.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,|\"op\":8,")
sim_script_test(sim_agent_hold_cancel_on_select agent_hold_cancel_on_select.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,")
# Sigil AGENT and READ (GLYPHS.md §5): every state of the two screens, named in the report; a touch on a disc past
# the centre zone is a touch on it; a tap or a short hold on Stop sends nothing (no op 6); the row touched is the
# reply sent ("No" = type arg 101); button A in the reader opens the session on the computer (op 0).
sim_script_test(sim_sigil_agent sigil_agent.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,"
                PASS_REGULAR_EXPRESSION "\"op\":11,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":101.*\"op\":0,\"sid\":2")
set_tests_properties(sim_sigil_agent PROPERTIES TIMEOUT 120)
sim_script_test(sim_sigil_agent_bare sigil_agent_bare.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,|\"op\":11,")
# Read before you approve: an unread long text locks Approve; the reader cannot answer; a daemon-cut text is
# never approvable on the watch.
sim_script_test(sim_ask_read_first ask_read_first.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_ask_read_then_approve ask_read_then_approve.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":3,\"sid\":2,\"rid\":7")
sim_script_test(sim_ask_cut_text ask_cut_text.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":4,\"sid\":2,\"rid\":7")
# Jobs (JOBS.md): terminal commands as cards; Stop and Run again only by a hold; Dismiss is the ack.
sim_script_test(sim_jobs_card jobs_card.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_jobs_wire jobs_wire.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_jobs_menu jobs_menu.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0,|\"op\":1,|\"op\":17,"
                PASS_REGULAR_EXPRESSION "\"op\":6,\"sid\":5")
sim_script_test(sim_jobs_rerun jobs_rerun.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0,|\"op\":1,|\"op\":6,"
                PASS_REGULAR_EXPRESSION "\"op\":17,\"sid\":5")
sim_script_test(sim_jobs_dismiss jobs_dismiss.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":6,|\"op\":17,"
                PASS_REGULAR_EXPRESSION "\"op\":1,\"sid\":5")
# Gauges (GAUGES.md): plan usage on STATUS and the idle faces, context fill and readouts on the card and AGENT.
sim_script_test(sim_gauges_status gauges_status.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_gauges_card gauges_card.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4")
sim_script_test(sim_gauges_face gauges_face.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_gauges_face PROPERTIES TIMEOUT 60)
# exactly one warning buzz: the crossing to 81 % (the quiet one is logged with quiet true and plays nothing)
sim_script_test(sim_gauges_buzz gauges_buzz.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"usage_warn\",\"pct\":(86|90)|\\[200\\].*\\[200\\]"
                PASS_REGULAR_EXPRESSION "\"usage_warn\",\"pct\":81,\"quiet\":false.*\"pattern_ms\":\\[200\\].*\"usage_warn\",\"pct\":85,\"quiet\":true")
sim_script_test(sim_gauges_wire gauges_wire.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# Done summary (DONE-SUMMARY.md, proto 0.14): the line on a finished card, tests failed, only the time, the width limit.
foreach(n done_all done_failed done_time_only done_long done_wire done_activity)
  sim_script_test(sim_${n} ${n}.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4")
endforeach()
# Sigil (GLYPHS.md §5 GAUGES, DONE): the arcs, the AGENT readout's glyphs and the done row in the states the scripts
# above do not render; the report keeps their words.
sim_script_test(sim_sigil_gauges sigil_gauges.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4")
set_tests_properties(sim_sigil_gauges PROPERTIES TIMEOUT 90)

# Answer window and full text (ANSWER-WINDOW.md, proto 0.11). No approve (op 3) may leave after a window ended,
# while the full text is still arriving, for a text that does not add up, past the cap or under privacy meta.
sim_script_test(sim_window_countdown window_countdown.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_window_expired window_expired.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
sim_script_test(sim_window_offline_expiry window_offline_expiry.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
sim_script_test(sim_window_handback window_handback.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3"
  PASS_REGULAR_EXPRESSION "\"tx_cmd\",\"id\":[0-9]+,\"op\":0,\"sid\":2,\"rid\":7")
sim_script_test(sim_window_old_daemon window_old_daemon.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# reminders grow louder over the window (issue 20261126-a909): nothing by 24 s (the `ui` dump), a soft
# pulse at 25 s, the pattern at 60 s, the pattern twice and the sound at 110 s
sim_script_test(sim_window_nag window_nag.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"nag\".*\"ev\":\"state\""
  PASS_REGULAR_EXPRESSION "\"ev\":\"state\".*\"nag\",\"rid\":7,\"level\":1.*pattern_ms\":\\[60\\].*\"nag\",\"rid\":7,\"level\":2.*pattern_ms\":\\[250,120,250\\].*\"nag\",\"rid\":7,\"level\":3.*pattern_ms\":\\[250,120,250,400,250,120,250\\].*\"ev\":\"sound\"")
set_tests_properties(sim_window_nag PROPERTIES TIMEOUT 60)
sim_script_test(sim_window_nag_short window_nag_short.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|pattern_ms\":\\[250,120,250\\].*pattern_ms\":\\[250,120,250\\]"
  PASS_REGULAR_EXPRESSION "pattern_ms\":\\[250,120,250\\]")
sim_script_test(sim_detail_read_approve detail_read_approve.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"op\":7,\"sid\":2,\"rid\":7.*\"ev\":\"done\",\"rid\":7.*\"op\":3,\"sid\":2,\"rid\":7")
sim_script_test(sim_detail_bound_to_rid detail_bound_to_rid.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_detail_wrong_total detail_wrong_total.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
sim_script_test(sim_detail_resume detail_resume.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"ask\",\"rid\":7,\"off\":1024.*\"ask\",\"rid\":7,\"off\":2048.*\"ask\",\"rid\":7,\"off\":3072.*\"op\":3,\"sid\":2,\"rid\":7")
set_tests_properties(sim_detail_resume PROPERTIES TIMEOUT 60)
sim_script_test(sim_detail_too_long detail_too_long.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":7|\"op\":3")
sim_script_test(sim_detail_privacy_meta detail_privacy_meta.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":7|\"op\":3")
# Standing rules (STANDING-RULES.md, proto 0.13): "Always allow" shows the rule in full before the hold, needs
# the 1600 ms grant hold, and sends approve with the hash of the rule on screen; a swapped rule, an expired or
# replaced request, a rule that cannot be shown in full and an old daemon never produce a grant (tx_rule).
sim_script_test(sim_rule_grant_hold rule_grant_hold.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4|\"op\":5|\"op\":3,\"sid\":2,\"rid\":12,\"hold_ms\":1[0-5][0-9][0-9],"
  PASS_REGULAR_EXPRESSION "\"tx_rule\",\"rid\":12,\"rh\":1365922541.*\"op\":3,\"sid\":2,\"rid\":12,\"hold_ms\":1[6-9][0-9][0-9],")
sim_script_test(sim_rule_session_exact rule_session_exact.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|tx_rule|\"op\":4"
  PASS_REGULAR_EXPRESSION "\"op\":3,\"sid\":2,\"rid\":14")
sim_script_test(sim_rule_swapped rule_swapped.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"tx_rule\",\"rid\":12,\"rh\":1365922541|\"op\":4"
  PASS_REGULAR_EXPRESSION "\"tx_rule\",\"rid\":12,\"rh\":1948751054")
sim_script_test(sim_rule_bound rule_bound.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|tx_rule|\"op\":3|\"op\":4")
sim_script_test(sim_rule_too_long rule_too_long.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|tx_rule|\"op\":3|\"op\":4")
sim_script_test(sim_rule_old_daemon rule_old_daemon.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|tx_rule")
sim_script_test(sim_rule_read_first rule_read_first.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4"
  PASS_REGULAR_EXPRESSION "\"tx_rule\",\"rid\":19,\"rh\":1365922541")
sim_script_test(sim_list_centre_drag list_centre_drag.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_list_rim_dial list_rim_dial.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_ask_second_tap ask_second_tap.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_ask_tap_open_row ask_tap_open_row.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":0,\"sid\":2")
sim_script_test(sim_ask_answered_stays ask_answered_stays.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_voice_long_transcript voice_long_transcript.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "voice_locked.*voice_page.*\"op\":12,\"sid\":1")
sim_script_test(sim_voice_minute_transcript voice_minute_transcript.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ms\":600[0-9][0-9],.*\"chars\":1515.*voice_locked.*\"page\":9.*\"op\":12,\"sid\":1")
set_tests_properties(sim_voice_minute_transcript PROPERTIES TIMEOUT 120)
sim_script_test(sim_voice_fit_transcript voice_fit_transcript.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":12,\"sid\":1")
sim_script_test(sim_queue_reply queue_reply.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":11,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":0,\"q\":1")
sim_script_test(sim_queue_dictate queue_dictate.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":12,\"sid\":1,\"rid\":0,\"hold_ms\":[0-9]+,\"arg\":[0-9]+,\"q\":1")
# The agent's last message (proto 0.18, LAST-MESSAGE.md): the card's preview and Read, the reader fetching it by
# session with no rid, the end picker; under privacy meta no text and no fetch; a new prompt closes the reader.
sim_script_test(sim_last_message_card last_message_card.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":(11|12|19)"
                PASS_REGULAR_EXPRESSION "\"op\":7,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":0.*\"ev\":\"done\",\"rid\":0,\"bytes\":[0-9]+,\"lm\":7")
sim_script_test(sim_last_message_reply last_message_reply.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":12,\"sid\":1,\"rid\":0,\"hold_ms\":[0-9]+,\"arg\":[0-9]+,\"q\":0")
# The agent graph (proto 0.22, GRAPH.md): Graph on the AGENT wheel, cmd graph on open, the text-free screen.
sim_script_test(sim_graph graph.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":21,\"sid\":1,\"rid\":0.*\"ev\":\"graph\",\"sid\":1,\"n\":16,\"drop\":3")
sim_script_test(sim_last_message_meta last_message_meta.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":7")
sim_script_test(sim_last_message_cleared last_message_cleared.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# Questions (AskUserQuestion, proto 0.18): a hold picks a single-choice option, taps tick multi-select ones and
# Send is held; "Other" dictates; every answer goes out at once (cmd answer, op 19); a tap never sends one.
sim_script_test(sim_question_single question_single.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"ev\":\"tx_answer\",\"rid\":31,\"n\":1,\"picks\":\\[2,.*\"pattern_ms\":\\[35,80,120\\]")
sim_script_test(sim_question_multi question_multi.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":19.*\"op\":19"
                PASS_REGULAR_EXPRESSION "\"ev\":\"tx_answer\",\"rid\":32,\"n\":2,\"picks\":\\[1,5,")
sim_script_test(sim_question_other question_other.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":12"
                PASS_REGULAR_EXPRESSION "\"ev\":\"tx_answer\",\"rid\":33,\"n\":1,\"picks\":\\[0,0,0,0\\],\"aids\":\\[[1-9]")
sim_script_test(sim_question_tap_never_sends question_tap_never_sends.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":19"
                PASS_REGULAR_EXPRESSION "\"op\":0,\"sid\":2,\"rid\":34")
sim_script_test(sim_queue_show_cancel queue_show_cancel.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":18,\"sid\":1")
# Sigil (GLYPHS.md): every primitive of ui_kit.h draws (the gallery), and HOME in each of its states keeps saying in
# the report what its glyphs mean; a tap on the hero glyph or the book disc opens ASK or READ and sends nothing.
sim_script_test(sim_kit_gallery kit_gallery.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_sigil_home sigil_home.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5")
set_tests_properties(sim_sigil_home PROPERTIES TIMEOUT 90)
# ASK in Sigil (docs/design/glyphs/ASK.md): every state as a render, the words each still says in the report, and
# a redraw budget at rest. A tap on the Approve disc sends nothing; the script's own holds approve, deny and answer.
sim_script_test(sim_sigil_ask sigil_ask.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":3.*\"op\":4.*\"op\":19")
set_tests_properties(sim_sigil_ask PROPERTIES TIMEOUT 120)
# Sigil: STATUS, LABS and TRACKPAD in every state (renders), the words each still reports, a tap on a disc in the
# rim zone, and STATUS at rest repainting nothing.
sim_script_test(sim_sigil_status sigil_status.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_sigil_status PROPERTIES TIMEOUT 90)
# Sigil: VOICE, KEYS and SCROLL in every state. The report keeps the words; a tap or a short A never sends (no
# dictate, op 12, at all; kb_send, op 15, only from the one A hold, with hold_ms >= 1000); a control answers the
# wrong gesture itself (voice_nudge, keys_flash); each screen at rest repaints nothing.
sim_script_test(sim_sigil_voice sigil_voice.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":12"
  PASS_REGULAR_EXPRESSION "voice_nudge.*voice_locked.*voice_page")
set_tests_properties(sim_sigil_voice PROPERTIES TIMEOUT 150)
sim_script_test(sim_sigil_keys sigil_keys.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":15,\"sid\":[0-9]+,\"rid\":0,\"hold_ms\":[0-9][0-9]?[0-9]?,"
  PASS_REGULAR_EXPRESSION "keys_flash.*\"op\":15,\"sid\":1,\"rid\":0,\"hold_ms\":1[0-9][0-9][0-9]")
sim_script_test(sim_sigil_scroll sigil_scroll.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"unit\":2")
sim_script_test(sim_deck_v3_states deck_v3_states.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_deck_v3_ambient deck_v3_ambient.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_deck_v3_ambient PROPERTIES TIMEOUT 60)
sim_script_test(sim_scroll_big scroll_big.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_scroll_big PROPERTIES TIMEOUT 60)
sim_script_test(sim_perf_four_agents perf_four_agents.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_perf_four_agents PROPERTIES TIMEOUT 60)
# Per-screen redraw budgets with four busy agents: HOME/STATUS/WATCH and the AGENT/ASK/SCROLL overlays.
sim_script_test(sim_perf_core_screens perf_core_screens.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_perf_core_screens PROPERTIES TIMEOUT 60)
sim_script_test(sim_perf_core_overlays perf_core_overlays.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_perf_core_overlays PROPERTIES TIMEOUT 60)
sim_script_test(sim_perf_ring_idle4 perf_ring_idle4.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_perf_ring_idle4 PROPERTIES TIMEOUT 60)
add_test(NAME sim_perf_ring_display_off COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/perf_ring_display_off.txt)
set_tests_properties(sim_perf_ring_display_off PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30 FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")

sim_script_test(sim_finished_buzz finished_buzz.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"glow\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"mode\",\"mode\":\"watch\"([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[35,200,35\\]([^p]|p[^a]|pa[^t])*\"ev\":\"snoozed\",\"rid\":8,([^p]|p[^a]|pa[^t])*\"ev\":\"attention\",\"rid\":9.*\"pattern_ms\":\\[250,120,250\\]")

# Finishes buzz by default (issue 20261126-27b7, proto 0.25 done_buzz): under all, a long turn or an error
# buzzes and a dark display glows, a burst buzzes once, a turn under 30 s is silent; armed is the old rule.
add_test(NAME sim_done_buzz_all
         COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/done_buzz_all.txt)
set_tests_properties(sim_done_buzz_all PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|finish_burst.*pattern_ms"
                     PASS_REGULAR_EXPRESSION "\"done_buzz\":1.*\"display_off\".*\"pattern_ms\":\\[35,200,35\\].*\"ev\":\"glow\".*\"pattern_ms\":\\[120,90,120,90,120\\].*\"ev\":\"glow\".*\"ev\":\"finish_burst\",\"sid\":4")
sim_script_test(sim_done_buzz_short done_buzz_short.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"ev\":\"glow\"|pattern_ms\":\\[35,200,35\\].*pattern_ms\":\\[35"
                PASS_REGULAR_EXPRESSION "\"short_turn\",\"sid\":2,\"secs\":5([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[35,200,35\\]")
sim_script_test(sim_done_buzz_armed done_buzz_armed.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"done_buzz\":0([^p]|p[^a]|pa[^t])*\"sid\":4,\"st\":\"done\"([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[35,200,35\\]([^p]|p[^a]|pa[^t])*\"done_buzz\":1([^p]|p[^a]|pa[^t])*\"short_turn\",\"sid\":2([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[35,200,35\\]")

# Presence (PRESENCE.md, proto 0.12): hush holds the arrival buzz, the reminders and the finished buzz; the
# card and ASK are as ever, and nothing is answered.
sim_script_test(sim_presence_hush_ask presence_hush_ask.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"ev\":\"glow\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"hush\",\"rid\":7,([^p]|p[^a]|pa[^t])*\"ev\":\"attention\",\"rid\":8.*pattern_ms\":\\[250,120,250\\]")
sim_script_test(sim_presence_hush_nag presence_hush_nag.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|pattern_ms\":\\[250,120,250\\]|\"ev\":\"nag\"|\"op\":3"
                PASS_REGULAR_EXPRESSION "\"ev\":\"hush\",\"rid\":7")
sim_script_test(sim_presence_hush_done presence_hush_done.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"glow\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"mode\",\"mode\":\"watch\"([^p]|p[^a]|pa[^t])*\"ev\":\"hush\",\"sid\":1,([^p]|p[^a]|pa[^t])*pattern_ms\":\\[35,200,35\\]")

# Link arbitration (hello on every up transport, first welcome wins) against a stubbed HAL.
add_executable(test_link_arb tests/test_link_arb.c ${FW}/core/src/link.c ${FW}/core/src/link_rx.c
               ${FW}/core/src/link_tx.c ${FW}/core/src/model.c ${FW}/core/src/steps.c ${FW}/core/src/gauges.c
               ${FW}/core/src/detail.c ${FW}/core/src/graph.c)
target_include_directories(test_link_arb PRIVATE ${FW}/core/src)
target_link_libraries(test_link_arb PRIVATE agentlink_hal agentlink_proto lvgl::lvgl m)
add_test(NAME link_arbitration COMMAND test_link_arb)
# A stand-in for the watch's internal-RAM limit (the simulator has none): core's static .data/.bss
# outside PSRAM (MODEL_EXT_BSS) must stay under budget. 2026-11-10: 20 KB more of session tables
# passed every test and boot-looped the watch. Host (64-bit) bytes; skipped without GNU `size`.
# Device check: firmware/tools/check_dram.py after idf.py build.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME core_ram_budget
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/ram_budget.py $<TARGET_FILE:core> 81920)
    set_tests_properties(core_ram_budget PROPERTIES SKIP_RETURN_CODE 77)
endif()

# ── Motion (docs/design/MOTION.md) ──
add_executable(test_motion tests/test_motion.c ${FW}/core/src/motion.c)
target_include_directories(test_motion PRIVATE ${FW}/core/src)
add_test(NAME motion_logic COMMAND test_motion)

# Raise to wake: the display (off after 1 s) lights on a raise from hanging to reading, once, and a bump on a
# desk does nothing; the pending permission request is left unanswered.
add_test(NAME sim_motion_raise_wake
         COMMAND agentlink_sim --headless --no-ws --idle-off 1 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/motion_raise_wake.txt)
set_tests_properties(sim_motion_raise_wake PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR} TIMEOUT 30
                     FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]|\"wake_motion\".*\"wake_motion\""
                     PASS_REGULAR_EXPRESSION "\"ev\":\"display_off\".*\"ev\":\"display_on\".*\"wake_motion\"")
# Face down: no buzz, no sound, no glow, no settings change; the display sleeps. Face up again, the next request buzzes and sounds.
sim_script_test(sim_motion_face_down motion_face_down.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|pattern_ms\":\\[[0-9]+,[0-9]|\"ev\":\"sound\"|\"ev\":\"glow\"|\"ev\":\"settings\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"attention\",\"rid\":7.*\"ev\":\"display_off\"")
sim_script_test(sim_motion_face_up_restores motion_face_up_restores.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"settings\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"attention\",\"rid\":7,([^p]|p[^a]|pa[^t])*\"ev\":\"attention\",\"rid\":8.*\"pattern_ms\":\\[250,120,250\\].*\"ev\":\"sound\"")
# Orientation: automatic with dwell and hysteresis, never during a touch; the stored setting; touch maths follow the turn.
sim_script_test(sim_motion_orientation motion_orientation.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"orientation\",\"flipped\":true.*\"ev\":\"orientation\",\"flipped\":false")
sim_script_test(sim_motion_orientation_stored motion_orientation_stored.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# Palm cover: the motor and speaker stop and the reminders are dropped; the request is not answered.
sim_script_test(sim_motion_cover_mute motion_cover_mute.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]|pattern_ms\":\\[250,120,250\\].*pattern_ms\":\\[250,120,250\\]|\"ev\":\"nag\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"sound\".*pattern_ms\":\\[\\].*\"ev\":\"sound_stop\".*\"ev\":\"cover_mute\"")
sim_script_test(sim_motion_cover_control motion_cover_nag_control.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"cover_mute\""
                PASS_REGULAR_EXPRESSION "pattern_ms\":\\[250,120,250\\].*\"nag\",\"rid\":7,\"level\":1.*pattern_ms\":\\[60\\].*\"nag\",\"rid\":7,\"level\":2.*pattern_ms\":\\[250,120,250\\]")
# Confirmation tones: rising on an approve hold, falling on a deny hold; none with Sounds off.
sim_script_test(sim_tone_approved approve_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":3.*\"ev\":\"sound\",\"samples\":4320")
sim_script_test(sim_tone_denied ask_deny_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[35]"
                PASS_REGULAR_EXPRESSION "\"op\":4.*\"ev\":\"sound\",\"samples\":5440")
sim_script_test(sim_tone_quiet confirm_tone_quiet.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"sound\""
                PASS_REGULAR_EXPRESSION "\"op\":4.*pattern_ms\":\\[60,90,60\\]")
sim_script_test(sim_volume_step confirm_tone_volume.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"volume\",\"percent\":25.*\"ev\":\"sound\"")
sim_script_test(sim_volume_zero confirm_tone_volume0.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"sound\"")

# OTA (PROTOCOL.md 6c): the Updating overlay, the transfer rules, the rollback guard. Events: ota_begin, ota_resume,
# ota_gap, ota_done, ota_bad, ota_stopped, ota_confirm, ota_busy; the simulator's own `restart` is the restart.
sim_script_test(sim_ota_happy ota_happy.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_begin\".*\"ev\":\"ota_done\".*\"ev\":\"ota_ack\",\"st\":1,\"next\":20000.*\"ev\":\"restart\"")
sim_script_test(sim_ota_gap ota_gap.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_gap\",\"off\":0,\"next\":4096.*\"ev\":\"ota_gap\",\"off\":8192,\"next\":4096.*\"ev\":\"ota_ack\",\"st\":0,\"next\":10240")
sim_script_test(sim_ota_resume ota_resume.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"ota_begin\".*\"ev\":\"ota_begin\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_resume\",\"next\":6144.*\"ev\":\"ota_done\"")
sim_script_test(sim_ota_redraw ota_redraw.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_ota_stop ota_stop.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_stopped\",\"why\":\"daemon\".*\"ev\":\"ota_ack\",\"st\":5")
sim_script_test(sim_ota_cancel_hold_b ota_cancel_hold_b.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_stopped\",\"why\":\"user\".*\"ev\":\"ota_ack\",\"st\":5")
sim_script_test(sim_ota_unsigned ota_unsigned.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_done|\"ev\":\"restart\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_bad\",\"st\":3,\"msg\":\"Signature not trusted\"")
sim_script_test(sim_ota_bad_header ota_bad_header.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_done"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_bad\",\"st\":3,\"msg\":\"Not an app image\"")
sim_script_test(sim_ota_bad_sha ota_bad_sha.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_done|ota_slot_ready"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_bad\",\"st\":3,\"msg\":\"Checksum mismatch\"")
sim_script_test(sim_ota_busy_low_battery ota_busy_low_battery.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_busy\",\"msg\":\"Battery too low\".*\"ev\":\"ota_ack\",\"st\":2.*\"ev\":\"ota_begin\"")
sim_script_test(sim_ota_busy_recording ota_busy_recording.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_begin"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_busy\",\"msg\":\"Recording a message\"")
sim_script_test(sim_ota_timeout ota_timeout.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_stopped\",\"why\":\"timeout\".*\"ev\":\"ota_ack\",\"st\":5")
sim_script_test(sim_ota_rollback_confirm ota_rollback_confirm.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_rollback|\"ev\":\"restart\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_unconfirmed\".*\"ev\":\"ota_confirm\".*\"ev\":\"ota_confirmed\"|\"ev\":\"ota_unconfirmed\".*\"ev\":\"ota_confirm\"")
set_tests_properties(sim_ota_rollback_confirm PROPERTIES ENVIRONMENT "AGENTLINK_SIM_UNCONFIRMED=1")
sim_script_test(sim_ota_rollback_timeout ota_rollback_timeout.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_confirm\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_rollback\".*\"ev\":\"restart\"")
set_tests_properties(sim_ota_rollback_timeout PROPERTIES ENVIRONMENT "AGENTLINK_SIM_UNCONFIRMED=1")
# Delta OTA (proto 0.21, issue ef2c): every patch in protocol/fixtures/ota (the reference detools' and the
# daemon's) rebuilds new.bin from base.bin on the device; a wrong base, a damaged patch and a resume.
# Events: ota_base_mismatch, ota_patch_error; ota_begin carries "patch".
file(GLOB OTA_PATCHES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/fixtures/ota/*.patch)
foreach(patch ${OTA_PATCHES})
    get_filename_component(pname ${patch} NAME_WE)
    sim_script_test(sim_ota_patch_${pname} ota_patch.txt
                    FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_bad|ota_patch_error"
                    PASS_REGULAR_EXPRESSION "\"ev\":\"ota_begin\",[^}]*\"patch\":[1-9].*\"ev\":\"ota_slot_ready\".*\"ev\":\"ota_done\".*\"ev\":\"ota_ack\",\"st\":1.*\"ev\":\"restart\""
                    ENVIRONMENT "AGENTLINK_SIM_PATCH=${patch}")
    sim_script_test(sim_ota_patch_${pname}_base_mismatch ota_patch_base_mismatch.txt
                    FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_slot_open|ota_begin"
                    PASS_REGULAR_EXPRESSION "\"ev\":\"ota_base_mismatch\".*\"ev\":\"ota_ack\",\"st\":3,\"next\":0}"
                    ENVIRONMENT "AGENTLINK_SIM_PATCH=${patch}")
    sim_script_test(sim_ota_patch_${pname}_corrupt ota_patch_corrupt.txt
                    FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_done|ota_slot_ready|\"ev\":\"restart\""
                    PASS_REGULAR_EXPRESSION "\"ev\":\"ota_bad\",\"st\":3,\"msg\":\"(Bad patch|Checksum mismatch|Too much data)\""
                    ENVIRONMENT "AGENTLINK_SIM_PATCH=${patch}")
    # Issue 203f: on a slow flash (25 ms per write) the decoder runs in steps of well under 100 ms.
    sim_script_test(sim_ota_patch_${pname}_slow ota_patch_slow.txt
                    FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_bad|ota_patch_error|ota_gap"
                    PASS_REGULAR_EXPRESSION "\"ev\":\"ota_done\",[^}]*\"steps\":([2-9]|[1-9][0-9]+),\"step_max_ms\":[0-9]?[0-9]}.*\"ev\":\"restart\""
                    ENVIRONMENT "AGENTLINK_SIM_PATCH=${patch}")
    set_property(TEST sim_ota_patch_${pname}_slow APPEND PROPERTY ENVIRONMENT "AGENTLINK_SIM_OTA_WRITE_US=25000")
    # ... and on a very slow one (300 ms per write, 3.6 s in all) a keepalive `go` repeats next = psize.
    sim_script_test(sim_ota_patch_${pname}_keepalive ota_patch_keepalive.txt
                    FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_bad|ota_patch_error|ota_gap"
                    PASS_REGULAR_EXPRESSION "\"ev\":\"ota_keepalive\".*\"ev\":\"ota_ack\",\"st\":0,\"next\":[0-9]+}.*\"ev\":\"ota_done\".*\"ev\":\"restart\""
                    ENVIRONMENT "AGENTLINK_SIM_PATCH=${patch}")
    set_property(TEST sim_ota_patch_${pname}_keepalive APPEND PROPERTY ENVIRONMENT "AGENTLINK_SIM_OTA_WRITE_US=300000")
    sim_script_test(sim_ota_patch_${pname}_resume ota_patch_resume.txt
                    FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"ota_begin\".*\"ev\":\"ota_begin\"|ota_bad"
                    PASS_REGULAR_EXPRESSION "\"ev\":\"ota_resume\",\"next\":2048.*\"ev\":\"ota_done\""
                    ENVIRONMENT "AGENTLINK_SIM_PATCH=${patch}")
endforeach()
# "Updated" tone (sound role `updated`): once, on the first welcome of an unconfirmed
# (OTA) boot; never on a normal boot, never again on a re-welcome.
sim_script_test(sim_ota_updated_tone ota_updated_tone.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"ota_updated_tone\".*\"ev\":\"ota_updated_tone\"|\"ev\":\"sound\".*\"ev\":\"sound\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"ota_confirm\".*\"ev\":\"ota_updated_tone\".*\"ev\":\"sound\"")
set_tests_properties(sim_ota_updated_tone PROPERTIES ENVIRONMENT "AGENTLINK_SIM_UNCONFIRMED=1")
sim_script_test(sim_ota_updated_tone_silent ota_updated_tone_silent.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|ota_updated_tone|\"ev\":\"sound\"")

# ── Power report (docs/design/POWER.md, proto 0.19) ──
# State-time accounting, cadence, refused sends, unlinked time and the figures a deep sleep keeps, against
# stubbed HAL / link / UI.
add_executable(test_power_report tests/test_power_report.c ${FW}/core/src/power_report.c)
target_include_directories(test_power_report PRIVATE ${FW}/core/src)
target_link_libraries(test_power_report PRIVATE agentlink_proto lvgl::lvgl m)
target_include_directories(test_power_report PRIVATE ${FW}/agentlink_hal/include)
add_test(NAME power_report_logic COMMAND test_power_report)
# The whole firmware: a report right after a 0.19 welcome (none before), the next 5 minutes later, over BLE with
# the interval; none to a 0.18 daemon; on / dim / off split; the deep sleep a boot ended; time while unlinked.
sim_script_test(sim_power_report_cadence power_report_cadence.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"report\".*\"ev\":\"welcome\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"welcome\".*\"ev\":\"report\",\"sent\":true,\"seq\":0,.*\"msg\":\"power\".*\"ev\":\"report\",\"sent\":true,\"seq\":1,\"mv\":4020,\"on\":30[0-9][0-9][0-9][0-9],\"dim\":[0-9]+,\"off\":0,\"deep\":0,\"lk\":30[0-9][0-9][0-9][0-9],\"ble\":30[0-9][0-9][0-9][0-9],\"itvl\":30000,\"nd\":0")
sim_script_test(sim_power_report_old_daemon power_report_old_daemon.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"report\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"welcome\"")
sim_power_test(sim_power_report_states power_report_states.txt ARGS --idle-off 100
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"report\",\"sent\":true,\"seq\":1,\"mv\":[0-9]+,\"on\":3[01][0-9][0-9][0-9],\"dim\":8[01][0-9][0-9][0-9],\"off\":18[89][0-9][0-9][0-9],\"deep\":0,")
sim_power_test(sim_power_report_deep power_report_deep.txt ARGS --battery --wake button --slept 7200
               PROPS FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                     PASS_REGULAR_EXPRESSION "\"ev\":\"report_restored\",\"kept\":false,\"seq\":0,\"deep\":7200000.*\"ev\":\"report\",\"sent\":true,\"seq\":0,\"mv\":[0-9]+,\"on\":[0-9]+,\"dim\":[0-9]+,\"off\":[0-9]+,\"deep\":7200000,.*\"nd\":1")
sim_script_test(sim_power_report_unlinked power_report_unlinked.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"ev\":\"report\",\"sent\":true,\"seq\":1,\"mv\":[0-9]+,\"on\":60[0-9][0-9][0-9][0-9],\"dim\":[0-9]+,\"off\":0,\"deep\":0,\"lk\":[0-9][0-9][0-9][0-9]?,")
