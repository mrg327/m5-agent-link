/* Link manager: handshake (hello → welcome → snapshot), incoming message dispatch into the
 * model, outgoing commands, message ids. Transport framing lives in the HAL. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"
#include "model.h"

typedef enum {
    LINK_OP_FOCUS = 0, LINK_OP_ACK = 1, LINK_OP_SNOOZE = 2, LINK_OP_APPROVE = 3, LINK_OP_DENY = 4,
    LINK_OP_REPLY = 5, LINK_OP_CANCEL = 6, LINK_OP_NOTIFY_DONE = 8, LINK_OP_SCROLL = 10, LINK_OP_TYPE = 11,
    LINK_OP_DICTATE = 12
} link_op_t;
#define LINK_OP_UNKNOWN (-1)

/* Notifications from the link to the app; all run inside link_on_frame()/link_tick(). */
typedef struct {
    void (*on_link)(bool welcomed);
    void (*on_attention)(const attention_t *att);
    void (*on_cleared)(uint32_t rid, int why);
    /* op is LINK_OP_UNKNOWN when the result does not match a command we sent. */
    void (*on_result)(int op, uint32_t sid, uint32_t rid, bool ok, int err, const char *msg);
    /* A result carried scroll_pos (UI-V2 §6.1). sid is 0 when the command it answers is no longer
     * pending (bursts overwrite the pending slots); the preview is "" when the daemon sent none. */
    void (*on_scroll_pos)(uint32_t sid, uint32_t lines_above, bool at_bottom, bool has_total, uint32_t total,
                          const char *preview);
    void (*on_cfg)(void);
    void (*on_transcript)(uint32_t aid, const char *text); /* voice: "" when nothing was heard */
} link_hooks_t;

void link_init(const link_hooks_t *hooks);
void link_on_state(hal_link_kind_t kind, bool connected);
/* Arbitration: until a welcome arrives, hellos go out on every transport that is up and the first
 * valid welcome picks the active link; afterwards frames from other transports are dropped and
 * outgoing messages go to the active one. Losing it restarts the cycle on the remaining transports. */
void link_on_frame_from(hal_link_kind_t kind, const uint8_t *frame, size_t len);
/* Legacy form without the source transport: attributed to link_active_kind(). */
void link_on_frame(const uint8_t *frame, size_t len);
void link_tick(uint32_t now_ms);
bool link_connected(void); /* at least one transport is up (UI: "connecting") */
bool link_welcomed(void);  /* handshake done on the active transport (UI: "linked") */
/* Transport carrying the link while welcomed; otherwise the first transport that is up. Gate on
 * link_welcomed() before showing it as the active link. */
hal_link_kind_t link_active_kind(void);

/* Returns false unless welcomed. */
bool link_send_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms);
/* Same, with a uint `arg` (snooze minutes, reply option index). */
/* Voice dictation (issue 0ea9): one recording is audio_start, audio blocks (<= 240 B of IMA ADPCM), audio_end. */
bool link_send_audio_start(uint32_t aid, uint32_t sid);
bool link_send_audio(uint32_t aid, uint32_t seq, const uint8_t *data, size_t len);
bool link_send_audio_end(uint32_t aid);
bool link_send_cmd_arg(link_op_t op, uint32_t sid, uint32_t rid, uint32_t arg, uint32_t hold_ms);
/* cmd_op_scroll with scroll_arg {unit, delta, seq}; unit is a nav_scroll_unit_t wire value. */
bool link_send_scroll(uint32_t sid, int unit, int delta, uint32_t seq);
