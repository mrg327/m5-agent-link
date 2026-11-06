idf_component_register(
    SRCS
        "board.cpp"
        "display.cpp"
        "input.cpp"
        "rtc.cpp"
        "touch.cpp"
    INCLUDE_DIRS "."
    REQUIRES agentlink_hal sw_power
    PRIV_REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_timer
        nvs_flash
        esp_app_format
        m5stack__m5gfx
        lvgl__lvgl
)
