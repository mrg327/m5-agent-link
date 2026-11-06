/* App internals shared with debug.c. */
#pragma once
#include <stdint.h>

#include "gesture.h"

uint32_t app_now_ms(void);
uint32_t app_fps(void);
/* Queue a synthetic gesture; it takes the same path as a real one (wake, log, UI). */
void app_inject_gesture(const gesture_evt_t *g);
