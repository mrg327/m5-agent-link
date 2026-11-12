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
    src/sim_board.c src/sim_display.c src/sim_link.c src/sim_ws.c src/sim_png.c src/sim_mic.c)
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

add_executable(test_steps tests/test_steps.c ${FW}/core/src/steps.c)
target_include_directories(test_steps PRIVATE ${FW}/core/src)
add_test(NAME step_timeline COMMAND test_steps)

# GAUGES.md: when a usage figure may be shown, the 80 % buzz rule, the words.
add_executable(test_gauges tests/test_gauges.c ${FW}/core/src/gauges.c ${FW}/core/src/model.c ${FW}/core/src/steps.c)
target_include_directories(test_gauges PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_gauges PRIVATE m)
add_test(NAME gauges_model COMMAND test_gauges)
# ANSWER-WINDOW.md: the full text bound to its rid, in order, resumed, failing closed; the window on the device's clock.
add_executable(test_detail tests/test_detail.c ${FW}/core/src/detail.c ${FW}/core/src/model.c ${FW}/core/src/steps.c)
target_include_directories(test_detail PRIVATE ${FW}/core/src ${FW}/agentlink_hal/include)
target_link_libraries(test_detail PRIVATE m)
add_test(NAME detail_model COMMAND test_detail)

add_executable(test_ring tests/test_ring.c ${FW}/core/src/ring.c)
target_include_directories(test_ring PRIVATE ${FW}/core/src)
add_test(NAME ring_fifo COMMAND test_ring)

add_executable(test_nav tests/test_nav.c ${FW}/core/src/nav.c)
target_include_directories(test_nav PRIVATE ${FW}/core/src)
add_test(NAME nav_model COMMAND test_nav)

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
sim_script_test(sim_deny_swipe deny_swipe.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4")
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
# UI-V2 Phase 3: ASK. Question options reply by tap; grants and Deny are holds; a tap never approves;
# a rejected answer restores the options; a hold is bound to its request id.
sim_script_test(sim_ask_reply ask_reply.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":8,\"hold_ms\":0,\"arg\":1")
sim_script_test(sim_ask_grant_hold ask_grant_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":9,\"hold_ms\":1[0-9][0-9][0-9],\"arg\":1")
sim_script_test(sim_ask_grant_hold_progress ask_grant_hold_progress.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"tx_cmd\"")
sim_script_test(sim_ask_deny_hold ask_deny_hold.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":4,\"sid\":2,\"rid\":7")
sim_script_test(sim_ask_tap_never_approves ask_tap_never_approves.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"op\":5"
                PASS_REGULAR_EXPRESSION "\"op\":0,\"sid\":2")
# UI-V3 §3.3: tapping any ASK row (selected or neighbour) never approves, denies or answers a hold item;
# tapping a neighbour question option sends that option's reply.
sim_script_test(sim_ask_tap_rows ask_tap_rows.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4|\"op\":5")
sim_script_test(sim_ask_tap_option_row ask_tap_option_row.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":0|\"op\":3|\"op\":4"
                PASS_REGULAR_EXPRESSION "\"op\":5,\"sid\":2,\"rid\":8,\"hold_ms\":0,\"arg\":1")
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
# KEYS refusals show as short reasons; an arriving request closes the keyboard (kb_end arg 1) into ASK.
sim_script_test(sim_keys_refusal keys_refusal.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
  PASS_REGULAR_EXPRESSION "\"op\":16,\"sid\":1,\"rid\":0,\"hold_ms\":0,\"arg\":1.*\"mode\":\"home\".*\"mode\":\"ask\"")
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
sim_script_test(sim_status status.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":4")
set_tests_properties(sim_status PROPERTIES TIMEOUT 60)
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
set_tests_properties(sim_watch_ask_return PROPERTIES TIMEOUT 60)
sim_script_test(sim_watch_states watch_states.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
set_tests_properties(sim_watch_states PROPERTIES TIMEOUT 60)
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

# Answer window and full text (ANSWER-WINDOW.md, proto 0.11). No approve (op 3) may leave after a window ended,
# while the full text is still arriving, for a text that does not add up, past the cap or under privacy meta.
sim_script_test(sim_window_countdown window_countdown.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
sim_script_test(sim_window_expired window_expired.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
sim_script_test(sim_window_offline_expiry window_offline_expiry.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3")
sim_script_test(sim_window_handback window_handback.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3"
  PASS_REGULAR_EXPRESSION "\"tx_cmd\",\"id\":[0-9]+,\"op\":0,\"sid\":2,\"rid\":7")
sim_script_test(sim_window_old_daemon window_old_daemon.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL")
# reminders spread over the window: arrival, nothing by 16 s (the `ui` dump), the first reminder at 30 s
sim_script_test(sim_window_nag window_nag.txt
  FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|pattern_ms\":\\[250,120,250\\].*pattern_ms\":\\[250,120,250\\].*\"ev\":\"state\""
  PASS_REGULAR_EXPRESSION "\"ev\":\"state\".*pattern_ms\":\\[250,120,250\\]")
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
sim_script_test(sim_voice_long_transcript voice_long_transcript.txt FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":12,")
sim_script_test(sim_voice_fit_transcript voice_fit_transcript.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL"
                PASS_REGULAR_EXPRESSION "\"op\":12,\"sid\":1")
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
                PASS_REGULAR_EXPRESSION "\"ev\":\"mode\",\"mode\":\"watch\"([^p]|p[^a]|pa[^t])*\"pattern_ms\":\\[30,200,30\\]([^p]|p[^a]|pa[^t])*\"ev\":\"snoozed\",\"rid\":8,([^p]|p[^a]|pa[^t])*\"ev\":\"attention\",\"rid\":9.*\"pattern_ms\":\\[250,120,250\\]")

# Presence (PRESENCE.md, proto 0.12): hush holds the arrival buzz, the reminders and the finished buzz; the
# card and ASK are as ever, and nothing is answered.
sim_script_test(sim_presence_hush_ask presence_hush_ask.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":3|\"op\":4|\"ev\":\"glow\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"hush\",\"rid\":7,([^p]|p[^a]|pa[^t])*\"ev\":\"attention\",\"rid\":8.*pattern_ms\":\\[250,120,250\\]")
sim_script_test(sim_presence_hush_nag presence_hush_nag.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|pattern_ms\":\\[250,120,250\\]|\"op\":3"
                PASS_REGULAR_EXPRESSION "\"ev\":\"hush\",\"rid\":7")
sim_script_test(sim_presence_hush_done presence_hush_done.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"glow\""
                PASS_REGULAR_EXPRESSION "\"ev\":\"mode\",\"mode\":\"watch\"([^p]|p[^a]|pa[^t])*\"ev\":\"hush\",\"sid\":1,([^p]|p[^a]|pa[^t])*pattern_ms\":\\[30,200,30\\]")

# Link arbitration (hello on every up transport, first welcome wins) against a stubbed HAL.
add_executable(test_link_arb tests/test_link_arb.c ${FW}/core/src/link.c ${FW}/core/src/link_rx.c
               ${FW}/core/src/link_tx.c ${FW}/core/src/model.c ${FW}/core/src/steps.c ${FW}/core/src/gauges.c
               ${FW}/core/src/detail.c)
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
# Palm cover: the motor and speaker stop and the 15 s reminder is dropped; the request is not answered.
sim_script_test(sim_motion_cover_mute motion_cover_mute.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"op\":[345]|pattern_ms\":\\[250,120,250\\].*pattern_ms\":\\[250,120,250\\]"
                PASS_REGULAR_EXPRESSION "\"ev\":\"sound\".*pattern_ms\":\\[\\].*\"ev\":\"sound_stop\".*\"ev\":\"cover_mute\"")
sim_script_test(sim_motion_cover_control motion_cover_nag_control.txt
                FAIL_REGULAR_EXPRESSION "SCRIPT FAIL|\"ev\":\"cover_mute\""
                PASS_REGULAR_EXPRESSION "pattern_ms\":\\[250,120,250\\].*pattern_ms\":\\[250,120,250\\]")
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
