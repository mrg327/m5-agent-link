#include "nav.h"

#include <stddef.h>

/* UI-V2 §3, one row per cell. Order matters only where two rows share a gesture and zone. */
const nav_binding_t nav_bindings[] = {
    /* HOME: the deck. Nothing here is destructive: no centre press can approve (UI-V2 §3). */
    {MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK}, /* opens the options: a tap never approves */
    {MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_OPEN_AGENT},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_OPEN_AGENT},
    {MODE_HOME, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_HOME, G_SWIPE_UP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK},
    {MODE_HOME, G_SWIPE_UP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_OPEN_AGENT},
    {MODE_HOME, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_ALWAYS, NAV_OPEN_STATUS}, /* deny lives only in ASK */
    {MODE_HOME, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_HOME, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_HOME, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_HOME, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_FOCUS},
    {MODE_HOME, G_BTN_A_DOUBLE, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT_ATTENTION},
    {MODE_HOME, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_HOME, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_PREV},
    /* AGENT: the per-agent wheel */
    {MODE_AGENT, G_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_PRESS},
    {MODE_AGENT, G_RELEASE, ZONE_ANY, WHEN_ALWAYS, NAV_HOLD_RELEASE},
    {MODE_AGENT, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_AGENT, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_AGENT, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_AGENT, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* only a long swipe counts */
    {MODE_AGENT, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_AGENT, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_AGENT, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_AGENT, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_AGENT, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_AGENT, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_AGENT, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* ASK: the request's options on the wheel. Approve-class and deny items are hold items; the
     * only tap commits are question options and Open on PC. */
    {MODE_ASK, G_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_PRESS},
    {MODE_ASK, G_RELEASE, ZONE_ANY, WHEN_ALWAYS, NAV_HOLD_RELEASE},
    {MODE_ASK, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT}, /* hold items answer "hold to confirm" */
    {MODE_ASK, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_ASK, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_ASK, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_LONG}, /* injected input only: must cover the hold */
    {MODE_ASK, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_REQUEST, NAV_DENY}, /* strict: long, from above the centre */
    {MODE_ASK, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_NO_REQUEST, NAV_BACK}, /* a question: back, request stays */
    {MODE_ASK, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_ASK, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_ASK, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_ASK, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_FOCUS}, /* A is "Open on PC" and never approves */
    {MODE_ASK, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_ASK, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_ASK, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* STATUS: the watch's own rows on the wheel (UI-V2 §4.5). Nothing here is destructive, so a tap
     * commits like A does; swipe up (the opposite of the swipe that opened it) goes back; swipe down
     * is ignored, so it can never read as deny. */
    {MODE_STATUS, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_STATUS, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_STATUS, G_SWIPE_UP, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_STATUS, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_STATUS, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_STATUS, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_STATUS, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_STATUS, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_STATUS, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_STATUS, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* WATCH: a monitor for one running session (UI-V2 §4.7). It follows the deck's selection, so the
     * session moves like on HOME (rim, B). Tap focuses the terminal, or opens ASK when the session
     * waits for you. Swipe up (like STATUS) goes back; swipe down is ignored, so it can never read as
     * deny. There is no timeout. */
    {MODE_WATCH, G_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK},
    {MODE_WATCH, G_TAP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_FOCUS},
    {MODE_WATCH, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_WATCH, G_SWIPE_UP, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_WATCH, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_WATCH, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_WATCH, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_WATCH, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_FOCUS},
    {MODE_WATCH, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_WATCH, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_WATCH, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
};
const int nav_n_bindings = (int)(sizeof nav_bindings / sizeof nav_bindings[0]);

nav_action_t nav_lookup(nav_mode_t mode, gesture_kind_t g, nav_zone_t zone, bool request_pending, bool askable)
{
    askable = askable || request_pending;
    for (int i = 0; i < nav_n_bindings; i++) {
        const nav_binding_t *b = &nav_bindings[i];
        if (b->mode != mode || b->gesture != g) continue;
        if (b->zone != ZONE_ANY && b->zone != zone) continue;
        if (b->when == WHEN_REQUEST && !request_pending) continue;
        if (b->when == WHEN_NO_REQUEST && request_pending) continue;
        if (b->when == WHEN_ASKABLE && !askable) continue;
        if (b->when == WHEN_NOT_ASKABLE && askable) continue;
        return b->action;
    }
    return NAV_NONE;
}

const char *nav_mode_name(nav_mode_t m)
{
    static const char *const names[MODE_COUNT] = {"home", "agent", "ask", "status", "watch"};
    return (unsigned)m < MODE_COUNT ? names[m] : "?";
}

const char *nav_action_name(nav_action_t a)
{
    static const char *const names[NAV_COUNT] = {
        "none", "focus", "open_agent", "open_ask", "open_status", "back", "home", "next", "prev", "next_attention", "pick_rim",
        "scrub", "commit", "deny", "hold_press", "hold_release", "hold_long"};
    return (unsigned)a < NAV_COUNT ? names[a] : "?";
}

bool nav_centre(int x, int y)
{
    int dx = x - GESTURE_CX, dy = y - GESTURE_CY;
    return dx * dx + dy * dy < NAV_CENTRE_R * NAV_CENTRE_R;
}

bool nav_hold_may_start(uint32_t shown_rid, uint32_t shown_since_ms, uint32_t rid, uint32_t now_ms)
{
    return rid && shown_rid == rid && now_ms - shown_since_ms >= NAV_CARD_ARM_MS;
}

bool nav_long_swipe_down(int y_start, int y_end)
{
    return y_start < GESTURE_CY && y_end - y_start >= NAV_LONG_SWIPE_PX;
}

int nav_agent_items(sess_state_t state, agent_item_t *out, int cap)
{
    /* At most three each (UI-V2 §4.2): Focus is button A on HOME and WATCH, Status the swipe down on
     * HOME, so neither takes a row; a session that waits for input keeps Focus, its only action. */
    static const agent_item_t work[] = {{AG_WATCH, false}, {AG_NOTIFY, false}, {AG_SNOOZE, false}};
    static const agent_item_t done[] = {{AG_ACK, false}, {AG_WATCH, false}};
    static const agent_item_t error[] = {{AG_DISMISS, false}, {AG_WATCH, false}};
    static const agent_item_t idle[] = {{AG_SNOOZE, false}};
    static const agent_item_t input[] = {{AG_FOCUS, false}};
    const agent_item_t *src;
    int n;
    switch (state) {
    case SESS_DONE: src = done; n = 2; break;
    case SESS_ERROR: src = error; n = 2; break;
    case SESS_IDLE: src = idle; n = 1; break;
    case SESS_INPUT: src = input; n = 1; break;
    default: src = work; n = 3; break;
    }
    int k = 0;
    for (int i = 0; i < n && k < cap; i++) out[k++] = src[i];
    return k;
}

int nav_ask_items(const attention_t *a, ask_item_t *out, int cap)
{
    int n = 0;
    if (!a || cap < 1) return 0;
    bool perm = a->kind == ATT_APPROVAL;
    if (a->n_opts > 0) {
        for (int i = 0; i < a->n_opts && n < cap - 1; i++) {
            bool grants = (a->grant_mask >> i) & 1u;
            /* in a permission every option answers the permission: the ones that do not grant deny it */
            uint32_t hold = grants ? NAV_HOLD_MS : perm ? NAV_DENY_HOLD_MS : 0;
            out[n++] = (ask_item_t){ASK_OPT, i, hold, grants};
        }
    } else if (perm) {
        if ((a->acts & (1u << ACT_APPROVE)) && n < cap - 1) out[n++] = (ask_item_t){ASK_APPROVE, -1, NAV_HOLD_MS, true};
        if ((a->acts & (1u << ACT_DENY)) && n < cap - 1) out[n++] = (ask_item_t){ASK_DENY, -1, NAV_DENY_HOLD_MS, false};
    }
    out[n++] = (ask_item_t){ASK_OPEN_PC, -1, 0, false}; /* always: the escape to the terminal */
    return n;
}

int nav_status_items(status_item_t *out, int cap)
{
    static const status_item_t rows[ST_COUNT] = {
        {ST_QUIET, true}, {ST_SOUNDS, true}, {ST_PRIVACY, false}, {ST_SESSIONS, false}, {ST_FIRMWARE, false}};
    int n = 0;
    for (int i = 0; i < ST_COUNT && n < cap; i++) out[n++] = rows[i];
    return n;
}

int nav_quiet_step(uint32_t quiet_until, uint32_t now_unix)
{
    if (!quiet_until || quiet_until <= now_unix) return 1;             /* off → 30 min */
    return quiet_until - now_unix <= NAV_QUIET_STEP1_S ? 2 : 0;         /* 30 min → 1 h → off */
}

uint32_t nav_quiet_next(uint32_t quiet_until, uint32_t now_unix)
{
    switch (nav_quiet_step(quiet_until, now_unix)) {
    case 1: return now_unix + NAV_QUIET_STEP1_S;
    case 2: return now_unix + NAV_QUIET_STEP2_S;
    default: return 0;
    }
}
