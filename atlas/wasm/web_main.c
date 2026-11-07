/* Exports for JS. JS owns the loop: call wasm_tick() from requestAnimationFrame. */
#include <emscripten.h>

#include "core.h"
#include "hal.h"
#include "web_hal.h"

#define RX_MAX 4096
static uint8_t s_rx[RX_MAX];

EMSCRIPTEN_KEEPALIVE void wasm_init(void)
{
    hal_board_init();
    core_init();
}
/* One core iteration (input, link, LVGL timers); returns ms the core suggests waiting. */
EMSCRIPTEN_KEEPALIVE int wasm_tick(void) { return (int)core_step(); }
EMSCRIPTEN_KEEPALIVE void wasm_pointer(int down, int x, int y) { web_pointer(down, x, y); }
EMSCRIPTEN_KEEPALIVE void wasm_button(int id, int down) { web_button(id, down); }
EMSCRIPTEN_KEEPALIVE void wasm_motion(int kind) { web_motion(kind); }
EMSCRIPTEN_KEEPALIVE uint8_t *wasm_rx_buf(void) { return s_rx; }
/* Frame bytes must already be in wasm_rx_buf(). */
EMSCRIPTEN_KEEPALIVE void wasm_deliver(int kind, int len)
{
    if (len > 0 && len <= RX_MAX) web_deliver(kind, s_rx, (size_t)len);
}
EMSCRIPTEN_KEEPALIVE void wasm_link_up(int kind, int up) { web_link_up(kind, up); }
EMSCRIPTEN_KEEPALIVE const uint32_t *wasm_fb_ptr(void) { return web_framebuffer(); }
/* Returns 1 and fills wasm_dirty_ptr() (x1,y1,x2,y2 inclusive) if pixels changed since last call. */
EMSCRIPTEN_KEEPALIVE int wasm_take_dirty(void) { return web_take_dirty(); }
EMSCRIPTEN_KEEPALIVE const int *wasm_dirty_ptr(void) { return web_dirty_rect(); }
EMSCRIPTEN_KEEPALIVE const char *wasm_screen_name(void) { return core_screen_name(); }
EMSCRIPTEN_KEEPALIVE int wasm_fps(void) { extern uint32_t app_fps(void); return (int)app_fps(); }
EMSCRIPTEN_KEEPALIVE void wasm_dump_ui_state(void) { core_dump_ui_state(); }
EMSCRIPTEN_KEEPALIVE void wasm_set_wallclock(unsigned unix_s) { web_set_wallclock(unix_s); }
