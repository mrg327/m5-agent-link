/* Shared by link.c, link_rx.c and link_tx.c only. */
#pragma once
#include "agentlink_codec_decode.h"
#include "agentlink_codec_encode.h"
#include "debug.h"
#include "link.h"
#include "ui.h"

#define LINK_MAX_MSG 4096 /* advertised as caps.max_msg */
#define LINK_MAX_PENDING 4
#define LINK_N_KINDS 3 /* hal_link_kind_t values */

typedef struct { uint32_t id; int op; uint32_t sid; } pending_cmd_t;

typedef struct {
    hal_link_kind_t kind; /* active transport; valid while welcomed */
    bool up[LINK_N_KINDS];
    bool connected; /* any transport up */
    bool welcomed;
    bool hello_cycle; /* hellos are being sent on every up transport */
    uint32_t tx_id, last_rx_id;
    uint32_t last_activity_ms, hello_ms[LINK_N_KINDS];
    pending_cmd_t pending[LINK_MAX_PENDING];
    link_hooks_t hooks;
} link_state_t;

extern link_state_t g_link;
/* Envelopes are large (fixed-size arrays for 32 sessions); keep them static, not on the stack. */
extern struct envelope_r g_rx_env, g_tx_env;

/* link_rx.c */
void link_rx_dispatch(const struct envelope_r *env, uint32_t now_ms);

/* link_tx.c */
bool link_tx_hello(hal_link_kind_t to);
bool link_tx_pong(uint32_t re);
bool link_tx_ping(void);
bool link_send_ui_state(const ui_report_t *r, uint32_t re);
bool link_send_screenshot_chunk(uint16_t w, uint16_t h, uint32_t offset, const uint8_t *data, size_t len,
                                bool last, uint32_t re);
