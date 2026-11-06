# Header-only interface component: core/ and the boards include "hal.h" through it.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl
)
