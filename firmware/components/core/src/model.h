/* Session store, attention queue and settings. Plain data, no LVGL, no protocol structs.
 * Enum values equal the wire values in protocol/agentlink.cddl so conversion is a cast. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "steps.h"

#define MODEL_MAX_SESSIONS 32
#define MODEL_MAX_ATTENTION 4

/* AGENT_SHELL is a job: a command the user started in a terminal (JOBS.md, proto 0.9). */
typedef enum { AGENT_COPILOT = 0, AGENT_CLAUDE = 1, AGENT_CLOUD = 2, AGENT_SHELL = 3, AGENT_OTHER = 15 } agent_t;
typedef enum {
    SESS_START = 0, SESS_WORK = 1, SESS_INPUT = 2, SESS_ERROR = 3,
    SESS_DONE = 4, SESS_IDLE = 5, SESS_ENDED = 6
} sess_state_t;
typedef enum { ATT_APPROVAL = 0, ATT_QUESTION = 1, ATT_ERROR = 2, ATT_DONE = 3 } att_kind_t;
typedef enum { ACT_APPROVE = 0, ACT_DENY = 1, ACT_FOCUS = 2, ACT_REPLY = 3, ACT_ACK = 4, ACT_DICTATE = 5 } att_action_t;
typedef enum { PRIVACY_META = 0, PRIVACY_PREVIEW = 1, PRIVACY_FULL = 2 } privacy_t;
typedef enum { ALERT_FULL = 0, ALERT_QUIET = 1, ALERT_SILENT = 2 } alert_mode_t;

/* Large static tables go to PSRAM on the device (internal DRAM is what BLE needs at boot). In the
 * simulator they get a section of their own, so simulator/tools/ram_budget.py can count everything
 * else against the internal-RAM budget. */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define MODEL_EXT_BSS EXT_RAM_BSS_ATTR
#elif defined(__ELF__)
#define MODEL_EXT_BSS __attribute__((section(".ext_ram.bss")))
#else
#define MODEL_EXT_BSS
#endif

typedef struct {
    uint32_t sid;
    agent_t agent;
    sess_state_t state;
    uint8_t sub; /* 0 approval, 1 question; meaningful when state == SESS_INPUT */
    char repo[33], branch[33], tool[25], summary[81];
    uint8_t host;  /* 0 = the daemon's PC, 1.. = a remote host */
    char hn[17];   /* remote host name; empty for host 0 or an old daemon */
    uint32_t t0, tst; /* unix seconds: start, last state change */
    bool seen;
    uint32_t snz;   /* snoozed until (unix); 0 = not snoozed */
    bool ntf;       /* "notify when done" is armed */
    uint32_t n;     /* tool steps in the current turn */
    uint8_t step;   /* step_state_t of step n: STEP_RUNNING while the daemon sent no `ok` */
    steps_t steps;  /* the last STEPS_MAX steps, built by model_upsert() from n / ok / the step text */
    bool no_scroll; /* device-side: the daemon answered a scroll with `unsupported`; hide the item */
    /* The agent's closing question and its answers for the Reply list (proto 0.5); "" / 0 = none. */
    char q[128];
    char rp[3][64];
    uint8_t n_rp;
    /* A job (AGENT_SHELL): the expected total run time, 0 = unknown, and the exit status once it ended. */
    uint32_t eta;
    uint8_t exit_code;
    /* Readouts from the agent's status line and hooks (GAUGES.md, proto 0.10). Each is shown only once
     * known: ctx_known false = no context figure (never drawn as 0 %); pm_known false = no mode reported;
     * pr 0 = no open pull request; mdl "" = model unknown. */
    bool ctx_known, pm_known;
    uint8_t ctx;  /* context window used, percent 0-100 */
    uint8_t pm;   /* perm_mode_t */
    uint8_t prs;  /* pr_state_t */
    uint32_t pr;
    char mdl[17];
    /* Device-side (ANSWER-WINDOW.md): this session's last request went back to the computer (its answer
     * window ran out, or Open on PC handed it back) and the terminal asks now. Cleared by a new request or a
     * change of state. */
    bool back;
    uint8_t back_why; /* BACK_EXPIRED / BACK_HANDED */
} session_t;
enum { BACK_EXPIRED = 0, BACK_HANDED = 1 };

/* Wire values of session.pm and session.prs (protocol/agentlink.cddl). */
typedef enum { PM_DEFAULT = 0, PM_PLAN = 1, PM_ACCEPT_EDITS = 2, PM_AUTO = 3, PM_DONT_ASK = 4, PM_BYPASS = 5 } perm_mode_t;
typedef enum { PRS_NONE = 0, PRS_PENDING = 1, PRS_APPROVED = 2, PRS_CHANGES = 3, PRS_DRAFT = 4 } pr_state_t;
#define MODEL_CTX_UNKNOWN 255 /* session.ctx on the wire: the figure is gone (after /compact) */

/* Which fields of a session delta were present on the wire. */
enum {
    SF_AGENT = 1 << 0, SF_STATE = 1 << 1, SF_SUB = 1 << 2, SF_REPO = 1 << 3, SF_BRANCH = 1 << 4,
    SF_TOOL = 1 << 5, SF_T0 = 1 << 6, SF_TST = 1 << 7, SF_SUMMARY = 1 << 8, SF_SEEN = 1 << 9,
    SF_NO_SCROLL = 1 << 10, /* not on the wire: set by the device itself */
    SF_HOST = 1 << 11, SF_HN = 1 << 12, SF_SNZ = 1 << 13, SF_NTF = 1 << 14, SF_N = 1 << 15,
    SF_OK = 1 << 16, /* with SF_OK, `step` is STEP_OK or STEP_FAIL */
    SF_Q = 1 << 17, SF_RP = 1 << 18, SF_ETA = 1 << 19, SF_EXIT = 1 << 20,
    SF_CTX = 1 << 21, SF_MDL = 1 << 22, SF_PM = 1 << 23, SF_PR = 1 << 24, SF_PRS = 1 << 25,
    SF_BACK = 1 << 26 /* not on the wire: set by the device (model_attention_went_back) */
};

typedef struct {
    uint32_t rid, sid;
    att_kind_t kind;
    char title[41], text[165];
    char opts[4][25];
    uint8_t n_opts;
    uint8_t acts; /* bitmask of 1 << att_action_t */
    uint8_t grant_mask; /* bit i set: opts[i] grants permission (approve-class, hold to commit) */
    uint32_t exp; /* unix, 0 = none */
    /* The answer window (proto 0.11, ANSWER-WINDOW.md), on the device's own millisecond clock: the daemon
     * sends what is left when it sends the request, so a reconnect or a clock difference cannot skew it. */
    uint32_t deadline_ms; /* when the window ends; 0 = no window known (an older daemon, or a notice) */
    uint32_t win_ms;      /* the whole window */
    /* The full text (proto 0.11): len_known says the daemon reports whether `text` is whole, and full_len is
     * the full text's length in bytes when it is not (0: `text` is the whole text). */
    bool len_known;
    uint32_t full_len;
    bool answer_sent; /* an answer (approve/deny/reply/dictate) is out; waiting for clear */
    uint8_t answer;   /* what was sent: ATT_ANSWER_* */
    uint8_t answer_opt; /* option index for ATT_ANSWER_REPLY */
} attention_t;
enum { ATT_ANSWER_NONE = 0, ATT_ANSWER_APPROVE, ATT_ANSWER_DENY, ATT_ANSWER_REPLY, ATT_ANSWER_DICTATE };

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
/* True while the session is snoozed. now_unix 0 = clock unknown: a snooze can never be judged, so off. */
bool model_session_snoozed(const session_t *s, uint32_t now_unix);
/* Called after every session upsert (daemon delta or device-side change) with a copy of the session as
 * it was (`before`, NULL for a new session), the session now, and the fields the delta carried. */
typedef void (*model_session_hook_t)(const session_t *before, const session_t *now, uint32_t present);
void model_set_session_hook(model_session_hook_t hook);
model_counts_t model_counts(void);

void model_attention_add(const attention_t *a);
bool model_attention_clear(uint32_t rid);
/* Drops every request (a new link: the daemon re-sends the open ones after its welcome). */
void model_attention_clear_all(void);
const attention_t *model_attention_for_sid(uint32_t sid);
const attention_t *model_attention_find(uint32_t rid);
const attention_t *model_attention_first(void);
/* Records the answer that went out (ATT_ANSWER_*); ATT_ANSWER_NONE restores the unanswered state
 * (the daemon rejected the command). */
void model_attention_set_answer(uint32_t rid, uint8_t answer, uint8_t opt);
void model_attention_expire(uint32_t now_unix);
/* Drops requests whose answer window ended more than MODEL_WINDOW_GRACE_MS ago without the daemon's clear
 * (the link is down): they went back to the computer. */
#define MODEL_WINDOW_GRACE_MS 1500
void model_attention_expire_ms(uint32_t now_ms);
/* Request `rid` went back to the computer (the daemon's clear expired / handed_back, or the local
 * deadline): its session is marked `back` (BACK_*), and the request removed. False if it was not there. */
bool model_attention_went_back(uint32_t rid, uint8_t why);
/* Milliseconds left in `a`'s answer window at now_ms; 0 when it has ended; UINT32_MAX when none is known. */
uint32_t model_attention_left_ms(const attention_t *a, uint32_t now_ms);
/* The daemon's protocol minor from its welcome (0 until one arrives). */
void model_set_peer_minor(uint8_t minor);
uint8_t model_peer_minor(void);

settings_t *model_settings(void);
/* The device changed a setting itself (STATUS): bumps the revision so the screens redraw. */
void model_settings_changed(void);
/* Something outside the session table changed (the plan usage, gauges.c): bumps the revision. */
void model_touch(void);
/* Timed quiet mode (UI-V2 §4.5): true while quiet_until lies in the future. Attention arrivals then
 * neither buzz, glow nor nag; interaction feedback (ticks, detents) is unaffected. */
bool model_quiet_active(uint32_t now_unix);
/* PC host name from the welcome message (empty when unknown); cleared with model_init(). */
void model_set_host(const char *host);
const char *model_host(void);
/* Quick replies from the welcome (proto 0.4, issue b150). `typing` is true when the daemon sent the
 * list at all: it can then type into terminals (quick replies and interrupt). Reset by each welcome. */
#define MODEL_MAX_REPLIES 6
void model_set_replies(const char *const *replies, int n, bool typing);
int model_reply_count(void);
const char *model_reply(int i);
bool model_can_type(void);
/* The daemon transcribes speech (welcome.voice, proto 0.5); reset by each welcome. */
void model_set_voice(bool on);
bool model_voice(void);
/* The daemon decodes swipe strokes (welcome.keys, proto 0.7); reset by each welcome. */
void model_set_keys(bool on);
bool model_keys(void);

const char *sess_state_name(sess_state_t s);
const char *agent_name(agent_t a);
void model_copy_str(char *dst, size_t cap, const uint8_t *src, size_t len);
