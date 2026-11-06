/* Display power policy: idle timeout, wake on input, brief glow for alerts while the display is off. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void power_init(void);
void power_set_idle_timeout_ms(uint32_t ms);
/* Any user input. Returns true if the display was off (the input only woke it and is consumed). */
bool power_activity(uint32_t now_ms);
/* An attention arrived: when the display is off, light a glow ring for a moment. */
void power_attention(uint32_t rgb, uint32_t now_ms);
void power_tick(uint32_t now_ms);
bool power_display_on(void);
const char *power_name(void);
