/* Link manager: handshake (hello → welcome → snapshot), incoming message dispatch into the
 * model, outgoing commands, message ids. Transport framing lives in the HAL. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gauges.h"
#include "hal.h"
#include "model.h"

typedef enum {
    LINK_OP_FOCUS = 0, LINK_OP_ACK = 1, LINK_OP_SNOOZE = 2, LINK_OP_APPROVE = 3, LINK_OP_DENY = 4,
    LINK_OP_REPLY = 5, LINK_OP_CANCEL = 6, LINK_OP_DETAIL = 7, LINK_OP_NOTIFY_DONE = 8, LINK_OP_SCROLL = 10, LINK_OP_TYPE = 11,
    LINK_OP_DICTATE = 12, LINK_OP_KB_PICK = 13, LINK_OP_KB_DEL = 14, LINK_OP_KB_SEND = 15, LINK_OP_KB_END = 16,
    LINK_OP_RERUN = 17, /* JOBS: run a finished job again */
    LINK_OP_UNQUEUE = 18, /* proto 0.17: drop the session's queued message */
    LINK_OP_ANSWER = 19,  /* proto 0.18: answer a request's questions at once (picks / aids) */
    LINK_OP_POINTER = 20, /* proto 0.20: the trackpad moves / clicks / scrolls the daemon's pointer */
    LINK_OP_GRAPH = 21    /* proto 0.22: ask for a session's agent graph (GRAPH.md) */
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
    /* KEYS (proto 0.7): candidates for a stroke; typed -1 when none was typed, err -1 when none (else a result_err value) */
    void (*on_kb_cands)(uint32_t kid, uint32_t seq, const char *const *words, int n, int typed, int err);
    /* GAUGES (proto 0.10): a new plan-usage reading, after the model holds it; `prev` is the one before. */
    void (*on_usage)(const usage_t *prev, const usage_t *now);
} link_hooks_t;

void link_init(const link_hooks_t *hooks);
/* A plan-usage reading (the `usage` message, GAUGES.md): into the model, then the on_usage hook. The
 * simulator's `usage` script command enters here too. */
void link_rx_usage(const usage_t *u);
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
/* Debug text to the daemon (lvl 0 err .. 3 debug); the trace keeps it. */
bool link_send_log(int lvl, const char *msg);
/* KEYS (issue 1e57): kb_start {kid, sid, keys} and kb_stroke {kid, seq, pts}; the byte layouts are PROTOCOL.md 6a. */
bool link_send_kb_start(uint32_t kid, uint32_t sid, const uint8_t *keys, size_t len);
bool link_send_kb_stroke(uint32_t kid, uint32_t seq, const uint8_t *pts, size_t len);
bool link_send_cmd_arg(link_op_t op, uint32_t sid, uint32_t rid, uint32_t arg, uint32_t hold_ms);
/* type / dictate with q: the daemon queues it when the session cannot take input now (proto 0.17; never a rid). */
bool link_send_cmd_queued(link_op_t op, uint32_t sid, uint32_t arg, uint32_t hold_ms);
/* cmd_op_approve with rh (proto 0.13, STANDING-RULES.md): approve and save the standing rule whose hash is rh. */
bool link_send_approve_rule(uint32_t sid, uint32_t rid, uint32_t hold_ms, uint32_t rh);
/* cmd_op_answer (proto 0.18, LAST-MESSAGE.md §3): per question of request rid, the options picked (a bit mask) and
 * the recording whose transcript is a free answer (0 = none); after a hold. n is 1..4. */
bool link_send_answer(uint32_t sid, uint32_t rid, const uint32_t *picks, const uint32_t *aids, int n, uint32_t hold_ms);
/* cmd_op_scroll with scroll_arg {unit, delta, seq}; unit is a nav_scroll_unit_t wire value. */
bool link_send_scroll(uint32_t sid, int unit, int delta, uint32_t seq);
/* cmd_op_pointer with pointer_arg (proto 0.20, EXPERIMENTS.md): move by (dx, dy), then click btn (-1 none, 0 left,
 * 1 right) with click count n (2 = the second click of a double click), then scroll wheel notches (down > 0). Returns
 * false unless welcomed by a daemon that offered the pointer. */
bool link_send_pointer(int dx, int dy, int btn, int n, int wheel);
/* cmd_op_graph (proto 0.22, GRAPH.md §3): ask for session sid's agent graph. Returns false unless welcomed by a
 * daemon with proto >= 0.22. */
bool link_send_graph(uint32_t sid);
/* Firmware update (proto 0.15): fw_ack {st, next, msg}; st is the wire value (0 go, 1 done, 2 busy, 3 bad, 4 fail, 5 stopped). */
bool link_send_fw_ack(int st, uint32_t next, const char *msg);
/* Power report (proto 0.19, POWER.md): voltages in mV, times in ms, itvl in us; field names as in the CDDL. */
typedef struct {
    uint32_t mv, mvf, vin, pct;
    bool chg, usb;
    uint32_t on, dim, off, deep, lk, ble, itvl, seq, nd;
} link_power_t;
/* Returns false unless welcomed by a daemon with proto >= 0.19, or when the transport refused the frame. */
bool link_send_power(const link_power_t *p);
#define LINK_POWER_MINOR 19
