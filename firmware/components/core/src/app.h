/* App internals shared with debug.c. */
#pragma once
#include <stdint.h>

#include "gesture.h"

uint32_t app_now_ms(void);
uint32_t app_fps(void);
/* Wall clock (UTC seconds, 0 = unknown); cheap, the RTC is only read every few seconds. */
uint32_t app_now_unix(void);
/* Re-read the RTC on the next app_now_unix(); call after hal_rtc_set_unix(). */
void app_clock_resync(void);
/* Queue a synthetic gesture; it takes the same path as a real one (wake, log, UI). */
void app_inject_gesture(const gesture_evt_t *g);
/* Number of selectable rim segments; the UI calls this whenever the ring is rebuilt. */
void app_set_sectors(int n);
