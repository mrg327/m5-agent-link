set(CORE_SRCS
    src/app.c src/debug.c src/detail.c src/feedback.c src/gauges.c src/gesture.c src/model.c src/steps.c src/power.c src/power_report.c src/ring.c
    src/link.c src/link_rx.c src/link_tx.c src/ota.c src/sha256.c src/ui_ota.c
    src/motion.c src/prefs.c
    src/nav.c src/ui.c src/ui_theme.c src/ui_ring.c src/ui_ring_draw.c src/ui_wheel.c src/ui_home.c src/ui_home_card.c
    src/ui_agent.c src/ui_ask.c src/ui_scroll.c src/ui_voice.c src/ui_read.c src/ui_graph.c src/graph.c src/voice.c src/ui_keys.c src/keys.c src/ui_status.c src/ui_labs.c src/ui_trackpad.c src/ui_gauges.c src/done.c src/ui_done.c src/ui_countdown.c src/ui_watch.c src/sound_bank.c src/sound.c
    src/fonts/font_figtree_20_500.c src/fonts/font_figtree_20_400.c src/fonts/font_figtree_20_600.c
    src/fonts/font_figtree_24_600.c src/fonts/font_figtree_34_600.c src/fonts/font_figtree_56_600.c
    src/fonts/font_phosphor_20.c src/fonts/font_phosphor_28.c src/fonts/font_phosphor_44.c
    third_party/detools/detools.c third_party/detools/heatshrink/heatshrink_decoder.c)

# Delta OTA (FIRMWARE.md 9): the detools patch decoder, heatshrink only, static buffers (window 2^11, lookahead
# 2^10: the format the daemon sends). The same values must reach ota.c, which holds the decoder.
set(DETOOLS_DEFS DETOOLS_CONFIG_FILE_IO=0 DETOOLS_CONFIG_COMPRESSION_NONE=0 DETOOLS_CONFIG_COMPRESSION_LZMA=0
    DETOOLS_CONFIG_COMPRESSION_CRLE=0 DETOOLS_CONFIG_COMPRESSION_HEATSHRINK=1 HEATSHRINK_DYNAMIC_ALLOC=0
    HEATSHRINK_STATIC_WINDOW_BITS=11 HEATSHRINK_STATIC_LOOKAHEAD_BITS=10 HEATSHRINK_STATIC_INPUT_BUFFER_SIZE=32)
set(DETOOLS_INC third_party/detools third_party/detools/heatshrink)

if(ESP_PLATFORM)
    # `lvgl__lvgl` is the name ESP-IDF's component manager gives the lvgl/lvgl managed component.
    idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS include PRIV_INCLUDE_DIRS src ${DETOOLS_INC}
                           REQUIRES agentlink_hal agentlink_proto lvgl__lvgl)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${DETOOLS_DEFS})
else()
    add_library(core STATIC ${CORE_SRCS})
    target_include_directories(core PUBLIC include PRIVATE src ${DETOOLS_INC})
    target_compile_definitions(core PRIVATE ${DETOOLS_DEFS})
    target_link_libraries(core PUBLIC agentlink_hal agentlink_proto lvgl::lvgl m)
    target_compile_options(core PRIVATE -Wall -Wextra)
endif()
