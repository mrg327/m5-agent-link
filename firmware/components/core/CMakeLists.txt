set(CORE_SRCS
    src/app.c src/debug.c src/feedback.c src/gesture.c src/model.c src/power.c src/ring.c
    src/link.c src/link_rx.c src/link_tx.c
    src/nav.c src/ui.c src/ui_theme.c src/ui_ring.c src/ui_wheel.c src/ui_home.c src/ui_agent.c src/ui_ask.c
    src/ui_scroll.c)

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
