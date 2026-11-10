idf_component_register(
    SRCS
        "audio.cpp"
        "board.cpp"
        "display.cpp"
        "input.cpp"
        "link.cpp"
        "link_ble.cpp"
        "link_usb.cpp"
        "mic.cpp"
        "rtc.cpp"
        "touch.cpp"
    INCLUDE_DIRS "."
    REQUIRES agentlink_hal sw_power
    WHOLE_ARCHIVE # hal_* impls are split across objects; core references them
    PRIV_REQUIRES
        driver
        bt
        esp_driver_usb_serial_jtag
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_timer
        esp_hw_support
        nvs_flash
        esp_app_format
        m5stack__m5gfx
        lvgl__lvgl
)

# The project builds at the IDF default -Og; the pixel path (LVGL's software renderer, M5GFX) gets -O2.
# Set AGENTLINK_GFX_O2=OFF to drop back to the global level. No-op under CONFIG_COMPILER_OPTIMIZATION_PERF.
option(AGENTLINK_GFX_O2 "Build LVGL and M5GFX with -O2" ON)
if(AGENTLINK_GFX_O2 AND CONFIG_COMPILER_OPTIMIZATION_DEBUG)
    foreach(_gfx lvgl__lvgl m5stack__m5gfx)
        idf_component_get_property(_gfx_lib ${_gfx} COMPONENT_LIB)
        target_compile_options(${_gfx_lib} PRIVATE -O2)
    endforeach()
endif()
