/* Link manager: handshake (hello → welcome → snapshot), incoming message dispatch into the
 * model, outgoing commands, message ids. Transport framing lives in the HAL. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal.h"
#include "model.h"

typedef enum {
    LINK_OP_FOCUS = 0, LINK_OP_ACK = 1, LINK_OP_APPROVE = 3, LINK_OP_DENY = 4
} link_op_t;
#define LINK_OP_UNKNOWN (-1)

/* Notifications from the link to the app; all run inside link_on_frame()/link_tick(). */
typedef struct {
    void (*on_link)(bool welcomed);
    void (*on_attention)(const attention_t *att);
    void (*on_cleared)(uint32_t rid, int why);
    /* op is LINK_OP_UNKNOWN when the result does not match a command we sent. */
    void (*on_result)(int op, uint32_t sid, bool ok, int err, const char *msg);
    void (*on_cfg)(void);
} link_hooks_t;

void link_init(const link_hooks_t *hooks);
void link_on_state(hal_link_kind_t kind, bool connected);
void link_on_frame(const uint8_t *frame, size_t len);
void link_tick(uint32_t now_ms);
bool link_connected(void); /* transport up */
bool link_welcomed(void);  /* handshake done */

/* Returns false if the link is down. */
bool link_send_cmd(link_op_t op, uint32_t sid, uint32_t rid, uint32_t hold_ms);
