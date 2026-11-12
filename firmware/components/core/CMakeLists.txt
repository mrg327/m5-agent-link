set(CORE_SRCS
    src/app.c src/debug.c src/feedback.c src/gesture.c src/model.c src/steps.c src/power.c src/ring.c
    src/link.c src/link_rx.c src/link_tx.c
    src/motion.c src/prefs.c
    src/nav.c src/ui.c src/ui_theme.c src/ui_ring.c src/ui_ring_draw.c src/ui_wheel.c src/ui_home.c src/ui_home_card.c
    src/ui_agent.c src/ui_ask.c src/ui_scroll.c src/ui_voice.c src/voice.c src/ui_keys.c src/keys.c src/ui_status.c src/ui_watch.c src/sound_bank.c src/sound.c
    src/fonts/font_figtree_16_500.c src/fonts/font_figtree_20_400.c src/fonts/font_figtree_20_600.c
    src/fonts/font_figtree_24_600.c src/fonts/font_figtree_34_600.c src/fonts/font_figtree_56_600.c
    src/fonts/font_phosphor_20.c src/fonts/font_phosphor_28.c src/fonts/font_phosphor_44.c)

if(ESP_PLATFORM)
    # `lvgl__lvgl` is the name ESP-IDF's component manager gives the lvgl/lvgl managed component.
    idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS include PRIV_INCLUDE_DIRS src
                           REQUIRES agentlink_hal agentlink_proto lvgl__lvgl)
else()
    add_library(core STATIC ${CORE_SRCS})
    target_include_directories(core PUBLIC include PRIVATE src)
    target_link_libraries(core PUBLIC agentlink_hal agentlink_proto lvgl::lvgl m)
    target_compile_options(core PRIVATE -Wall -Wextra)
endif()
