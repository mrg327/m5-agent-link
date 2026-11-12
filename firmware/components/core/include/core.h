/* Public entry points of the portable core. The board's main task (or the simulator's main loop)
 * calls core_init() once and then core_step() forever. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One ~60 ms buzz after the display is initialised, to confirm the motor works. Set to 0 to disable. */
#ifndef CORE_BOOT_BUZZ
#define CORE_BOOT_BUZZ 1
#endif

/* Brings up LVGL, the HAL display/touch/buttons/motion/link, and the UI. Call after hal_board_init(). */
void core_init(void);
/* One iteration: input, link, LVGL timers, debug. Returns ms the caller may sleep (always <= 10). */
uint32_t core_step(void);
/* 0 disables the idle display-off timeout. */
void core_set_idle_timeout_ms(uint32_t ms);
/* Display-off time before DEEP on battery with no link (default 10 min). */
void core_set_deep_after_ms(uint32_t ms);
/* Emits the current ui_state as a debug line. */
void core_dump_ui_state(void);
/* Test hooks for scripted runs: current screen name, and whether a visible label contains `text`. */
const char *core_screen_name(void);
bool core_ui_has_label(const char *text);
/* True while the UI is drawn turned by 180 degrees (orientation setting or automatic). */
bool core_display_flipped(void);
/* Pixels handed to the display's flush since boot: the redraw cost, for tests and profiling. */
uint32_t core_px_flushed(void);
