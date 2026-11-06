/* Session store, attention queue and settings. Plain data, no LVGL, no protocol structs.
 * Enum values equal the wire values in protocol/agentlink.cddl so conversion is a cast. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODEL_MAX_SESSIONS 32
#define MODEL_MAX_ATTENTION 4

typedef enum { AGENT_COPILOT = 0, AGENT_CLAUDE = 1, AGENT_CLOUD = 2, AGENT_OTHER = 15 } agent_t;
typedef enum {
    SESS_START = 0, SESS_WORK = 1, SESS_INPUT = 2, SESS_ERROR = 3,
    SESS_DONE = 4, SESS_IDLE = 5, SESS_ENDED = 6
} sess_state_t;
typedef enum { ATT_APPROVAL = 0, ATT_QUESTION = 1, ATT_ERROR = 2, ATT_DONE = 3 } att_kind_t;
typedef enum { ACT_APPROVE = 0, ACT_DENY = 1, ACT_FOCUS = 2, ACT_REPLY = 3, ACT_ACK = 4 } att_action_t;
typedef enum { PRIVACY_META = 0, PRIVACY_PREVIEW = 1, PRIVACY_FULL = 2 } privacy_t;
typedef enum { ALERT_FULL = 0, ALERT_QUIET = 1, ALERT_SILENT = 2 } alert_mode_t;

typedef struct {
    uint32_t sid;
    agent_t agent;
    sess_state_t state;
    uint8_t sub; /* 0 approval, 1 question; meaningful when state == SESS_INPUT */
    char repo[33], branch[33], tool[25], summary[81];
    uint32_t t0, tst; /* unix seconds: start, last state change */
    bool seen;
} session_t;

/* Which fields of a session delta were present on the wire. */
enum {
    SF_AGENT = 1 << 0, SF_STATE = 1 << 1, SF_SUB = 1 << 2, SF_REPO = 1 << 3, SF_BRANCH = 1 << 4,
    SF_TOOL = 1 << 5, SF_T0 = 1 << 6, SF_TST = 1 << 7, SF_SUMMARY = 1 << 8, SF_SEEN = 1 << 9
};

typedef struct {
    uint32_t rid, sid;
    att_kind_t kind;
    char title[41], text[165];
    char opts[4][25];
    uint8_t n_opts;
    uint8_t acts; /* bitmask of 1 << att_action_t */
    uint32_t exp; /* unix, 0 = none */
    bool answer_sent; /* approve/deny already sent; waiting for clear */
} attention_t;

typedef struct { uint32_t waiting, error, running, done_unseen; } model_counts_t;

typedef struct {
    privacy_t privacy;
    alert_mode_t alert;
    uint32_t quiet_until;
    int32_t tz_off_min;
    char handle[25];
} settings_t;

void model_init(void);
/* Incremented on every change; the UI re-renders when it differs from what it last drew. */
uint32_t model_rev(void);

void model_replace_all(const session_t *list, size_t n); /* snapshot */
void model_upsert(const session_t *delta, uint32_t present);
void model_remove(uint32_t sid);

size_t model_count(void);
const session_t *model_find(uint32_t sid);
/* Fills idx[] (size >= MODEL_MAX_SESSIONS) with session slots in display priority order. */
size_t model_sorted(uint8_t *idx);
const session_t *model_at(uint8_t slot);
model_counts_t model_counts(void);

void model_attention_add(const attention_t *a);
bool model_attention_clear(uint32_t rid);
const attention_t *model_attention_for_sid(uint32_t sid);
const attention_t *model_attention_find(uint32_t rid);
const attention_t *model_attention_first(void);
void model_attention_mark_sent(uint32_t rid);
void model_attention_expire(uint32_t now_unix);

settings_t *model_settings(void);

const char *sess_state_name(sess_state_t s);
const char *agent_name(agent_t a);
void model_copy_str(char *dst, size_t cap, const uint8_t *src, size_t len);
