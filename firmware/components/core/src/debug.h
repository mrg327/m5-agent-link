/* Observability (DESIGN §0): every state change and input becomes one JSON line on the debug
 * channel; the protocol's debug messages (ui_state, screenshot, inject_input) are served here. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { DBG_ERR = 0, DBG_WARN = 1, DBG_INFO = 2, DBG_DEBUG = 3 } dbg_level_t;

/* {"ts_us":..,"cat":cat,"ev":ev,<fields>}; `fields_fmt` is printf-style JSON members without
 * braces or a leading comma, or NULL. Strings must go through dbg_q(). */
void dbg_event(const char *cat, const char *ev, const char *fields_fmt, ...)
    __attribute__((format(printf, 3, 4)));
void dbg_event_at(dbg_level_t lvl, const char *cat, const char *ev, const char *fields_fmt, ...)
    __attribute__((format(printf, 4, 5)));
/* Quoted + escaped copy of s in a rotating static buffer (valid for the next few calls). */
const char *dbg_q(const char *s);
void dbg_set_level(dbg_level_t lvl);

/* Debug protocol messages. `re` is the request's message id. */
void dbg_on_ui_state_req(uint32_t re);
void dbg_on_screenshot_req(uint32_t re);
void dbg_on_inject(int input_event, bool has_xy, int x, int y);
/* Called every core_step: streams pending screenshot chunks, a few per call. */
void dbg_tick(uint32_t now_ms);
/* Emit the current ui_state as a debug line (used by simulator scripts). */
void dbg_dump_ui_state(void);
