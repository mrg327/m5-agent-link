/* Display power policy: idle timeout, wake on input, brief glow for alerts while the display is
 * off. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void power_init(void);
/* Call once the display and UI exist: a timer wake (reconnect attempt) turns the display off. */
void power_start(uint32_t now_ms);
bool power_woke_by_timer(void);
/* Display-off time before DEEP (battery, no link). Default 10 min; tests shorten it. */
void power_set_deep_after_ms(uint32_t ms);
void power_set_idle_timeout_ms(uint32_t ms);
/* Any user input. Returns true if the display was off (the input only woke it and is consumed). */
bool power_activity(uint32_t now_ms);
/* An attention arrived (or a reminder fired): when the display is off, light the glow for 3 s with
 * the kind's colour (rgb), session sid's sigil and the kind's glyph (icon, an ICON_* literal);
 * while it glows, the glow takes the new content and starts its 3 s again. A lit display only
 * counts it as activity. */
void power_attention(uint32_t rgb, uint32_t sid, const char *icon, uint32_t now_ms);
bool power_glowing(void);
void power_tick(uint32_t now_ms);
bool power_display_on(void);
const char *power_name(void);
