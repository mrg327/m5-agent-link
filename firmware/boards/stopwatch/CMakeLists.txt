idf_component_register(
    SRCS
        "board.cpp"
        "display.cpp"
        "input.cpp"
        "rtc.cpp"
        "touch.cpp"
    INCLUDE_DIRS "."
    REQUIRES agentlink_hal
    PRIV_REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_timer
        nvs_flash
        esp_app_format
        m5stack__m5gfx
        m5stack__M5IOE1
        m5stack__M5PM1
        lvgl__lvgl
)
