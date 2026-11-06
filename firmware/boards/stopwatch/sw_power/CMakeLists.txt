# Must NOT depend on m5gfx: see the comment at the top of sw_power.cpp.
idf_component_register(
    SRCS "sw_power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c
    PRIV_REQUIRES esp_driver_gpio m5stack__M5PM1 m5stack__M5IOE1
)
