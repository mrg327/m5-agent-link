/* The full text of one request (ANSWER-WINDOW.md §4, proto 0.11). A request whose `text` is a cut preview
 * carries `len`, the full text's length; the watch asks for it (cmd detail {rid, arg: offset}) and the daemon
 * answers with `detail` chunks of at most 1024 bytes, in order, then a result. One request at a time, bound
 * to its rid: a chunk for another rid, out of order, or that disagrees with the length the request announced
 * is dropped. A transfer that stalls (a lost link) asks again from where it got to. Plain data: no LVGL. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "model.h"

/* The longest full text the watch takes: 4 KiB, about twenty pages of the reader. Beyond it the request
 * stays "Too long to approve here". Must match DETAIL_CAP in the daemon's broker.rs. */
#define DETAIL_CAP 4096
/* A transfer with no chunk for this long asks again from where it got to (up to DETAIL_TRIES times). */
#define DETAIL_STALL_MS 3000
#define DETAIL_TRIES 4

typedef enum {
    DETAIL_NONE = 0, /* nothing asked for this rid */
    DETAIL_LOADING,  /* asked; chunks arriving */
    DETAIL_DONE,     /* the whole text is here */
    DETAIL_FAILED    /* refused (stale, denied, unsupported), too long, or it never came */
} detail_state_t;

/* Whether `a` has a full text the watch can fetch: the daemon said the preview is not whole, it fits the
 * cap, and the privacy setting allows content. */
bool detail_fetchable(const attention_t *a);
/* Starts (or resumes after a lost link) the transfer for `a`. No-op when it is done, failed or running. */
void detail_want(const attention_t *a, uint32_t now_ms);
/* One `detail` chunk off the wire. */
void detail_on_chunk(uint32_t rid, uint32_t sid, uint32_t off, uint32_t total, const uint8_t *text, uint32_t len,
                     uint32_t now_ms);
/* The daemon's result for a `cmd detail` (rid 0: a last message's, for session sid). */
void detail_on_result(uint32_t sid, uint32_t rid, bool ok, int err);
/* Resumes a stalled transfer; forgets one whose request is gone. Call every step. */
void detail_tick(uint32_t now_ms);

detail_state_t detail_state(uint32_t rid);
/* The text received so far for `rid` (a prefix of the full text while loading), or NULL. */
const char *detail_text(uint32_t rid);
uint32_t detail_got(uint32_t rid);
uint32_t detail_total(uint32_t rid);
void detail_reset(void);

/* A session's last message (proto 0.18, LAST-MESSAGE.md §2.3), in a slot of its own: asked for with `cmd detail
 * {sid, arg: offset}` and no rid; its chunks carry the message id `lm` and are kept only for the session and
 * message asked for, in order, with the announced length (session.ll). A replaced or cleared message is
 * forgotten. */
bool detail_msg_fetchable(const session_t *s);
void detail_want_msg(const session_t *s, uint32_t now_ms);
void detail_on_msg_chunk(uint32_t sid, uint32_t lm, uint32_t off, uint32_t total, const uint8_t *text, uint32_t len,
                         uint32_t now_ms);
detail_state_t detail_msg_state(uint32_t sid, uint32_t lm);
const char *detail_msg_text(uint32_t sid, uint32_t lm);
uint32_t detail_msg_got(uint32_t sid, uint32_t lm);
uint32_t detail_msg_total(uint32_t sid, uint32_t lm);
/* How a request goes out (the app wires it to link_send_cmd_arg(LINK_OP_DETAIL, …); rid 0 for a last message);
 * false = not sent. */
typedef bool (*detail_send_fn)(uint32_t sid, uint32_t rid, uint32_t off);
void detail_set_sender(detail_send_fn send);
