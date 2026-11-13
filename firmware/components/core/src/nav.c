#include "nav.h"

#include <stddef.h>
#include <stdlib.h>

/* UI-V2 §3, one row per cell. Order matters only where two rows share a gesture and zone. */
const nav_binding_t nav_bindings[] = {
    /* HOME: the deck. Nothing here is destructive: no centre press can approve (UI-V2 §3). */
    {MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK}, /* opens the options: a tap never approves */
    {MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_OPEN_AGENT},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_OPEN_AGENT},
    {MODE_HOME, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_HOME, G_SWIPE_UP, ZONE_CENTRE, WHEN_ALWAYS, NAV_OPEN_LABS}, /* experiments (EXPERIMENTS.md); tap opens AGENT / ASK */
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
    {MODE_AGENT, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG}, /* the list follows the finger */
    {MODE_AGENT, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_LONG}, /* held still off the rows: back */
    {MODE_AGENT, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: swipe right goes back */
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
    {MODE_ASK, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_LONG}, /* off the rows: back; on a row: injected input covering the hold */
    {MODE_ASK, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG}, /* the list follows the finger; no swipe denies */
    {MODE_ASK, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: back; the request stays pending, never deny */
    {MODE_ASK, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_ASK, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_ASK, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_ASK, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_FOCUS}, /* A is "Open on PC" and never approves */
    {MODE_ASK, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_ASK, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_ASK, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* SCROLL: the centre is a trackpad for the terminal (UI-V2 §4.4). The rim is unused, so nothing
     * here is a scrub, and a swipe is only its drag steps plus the glide, never deny or back. */
    {MODE_SCROLL, G_PRESS, ZONE_ANY, WHEN_ALWAYS, NAV_SCROLL_CATCH},
    {MODE_SCROLL, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG},
    {MODE_SCROLL, G_DRAG_END, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_FLING},
    {MODE_SCROLL, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* leaves the terminal where it is; a tap that caught a glide only stops it */
    {MODE_SCROLL, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: swipe right goes back */
    {MODE_SCROLL, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_TO_BOTTOM},
    {MODE_SCROLL, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_PAGE_NEWER},
    {MODE_SCROLL, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_SCROLL, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* STATUS: the watch's own rows on the wheel (UI-V2 §4.5). Nothing here is destructive, so a tap
     * commits like A does; swipe up (the opposite of the swipe that opened it) goes back; swipe down
     * is ignored, so it can never read as deny. */
    {MODE_STATUS, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_STATUS, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_STATUS, G_SWIPE_UP, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_STATUS, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_LONG}, /* held still off the toggles: back */
    {MODE_STATUS, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: swipe right goes back */
    {MODE_STATUS, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_STATUS, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_STATUS, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_STATUS, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_STATUS, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_STATUS, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_STATUS, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* WATCH: a monitor for one running session (UI-V2 §4.7). It follows the deck's selection, so the
     * session moves like on HOME (rim, B). Tap focuses the terminal, or opens ASK when the session
     * waits for you. A vertical drag in the centre scrolls the session's terminal: it opens SCROLL over
     * WATCH, and SCROLL returns here (issue 5cbb). B held or A+B goes back. There is no timeout. */
    {MODE_WATCH, G_TAP, ZONE_CENTRE, WHEN_ASKABLE, NAV_OPEN_ASK},
    {MODE_WATCH, G_TAP, ZONE_CENTRE, WHEN_NOT_ASKABLE, NAV_FOCUS},
    {MODE_WATCH, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_WATCH, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG}, /* scrolls the terminal (issue 5cbb) */
    {MODE_WATCH, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_WATCH, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_WATCH, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_WATCH, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_FOCUS},
    {MODE_WATCH, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_WATCH, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_WATCH, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* hold still 600 ms: back (issue dd44) */
    {MODE_WATCH, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: swipe right goes back */
    {MODE_WATCH, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* VOICE: dictate a reply (issue 0ea9). Tap or A stops the recording (or retries a failed one);
     * only the hold sends the transcript; a long swipe down or B held discards it. */
    {MODE_VOICE, G_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_PRESS},
    {MODE_VOICE, G_RELEASE, ZONE_ANY, WHEN_ALWAYS, NAV_HOLD_RELEASE},
    {MODE_VOICE, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_VOICE, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* only a long swipe counts */
    {MODE_VOICE, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: swipe right goes back */
    {MODE_VOICE, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_VOICE, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_VOICE, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* KEYS: the swipe keyboard (issue 1e57). Touch belongs to the stroke recorder, so only buttons
     * are bound: A held 1.2 s sends (the hold starts at the 0.6 s long-press event), B deletes the
     * last word, B held leaves and erases what was not sent. */
    {MODE_KEYS, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_KEYS, G_BTN_A_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_HOLD_PRESS},
    {MODE_KEYS, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_ERASE},
    {MODE_KEYS, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_KEYS, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* READ: the agent's last message in the reader (LAST-MESSAGE.md §1), then Dictate / Reply. Nothing on a page
     * sends anything: a tap, swipe up or B turns the page; the last one leads to the end picker, whose rows only
     * open VOICE or the Reply list (the commits are there). */
    {MODE_READ, G_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_PRESS},
    {MODE_READ, G_RELEASE, ZONE_ANY, WHEN_ALWAYS, NAV_HOLD_RELEASE},
    {MODE_READ, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_READ, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_READ, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG}, /* a page per drag; the picker follows the finger */
    {MODE_READ, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_LONG}, /* held still: back */
    {MODE_READ, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* UI-V3 §5: swipe right goes back */
    {MODE_READ, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_READ, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_READ, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_READ, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_FOCUS}, /* Open on PC, as on the deck */
    {MODE_READ, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_READ, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_READ, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* LABS: the experiments menu (EXPERIMENTS.md), a list like STATUS; swipe down undoes the swipe up */
    {MODE_LABS, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_LABS, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_LABS, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_LABS, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_LABS, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_LONG}, /* held still off the rows: back */
    {MODE_LABS, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_LABS, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_LABS, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_LABS, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_COMMIT},
    {MODE_LABS, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT},
    {MODE_LABS, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_LABS, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* TRACKPAD: the whole glass is the pad, the bezel the wheel. Swipes are only fast moves and a hold is the
     * right click, so only the buttons leave. */
    {MODE_TRACKPAD, G_MOVE, ZONE_ANY, WHEN_ALWAYS, NAV_POINTER},
    {MODE_TRACKPAD, G_TAP, ZONE_ANY, WHEN_ALWAYS, NAV_POINTER},
    {MODE_TRACKPAD, G_DOUBLE_TAP, ZONE_ANY, WHEN_ALWAYS, NAV_POINTER},
    {MODE_TRACKPAD, G_LONG_PRESS, ZONE_ANY, WHEN_ALWAYS, NAV_POINTER},
    {MODE_TRACKPAD, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_TRACKPAD, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_TRACKPAD, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_TRACKPAD, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_TRACKPAD, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_TRACKPAD, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
    /* GRAPH (GRAPH.md §4): the rim and a centre drag move through the rows; everything else goes back */
    {MODE_GRAPH, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG},
    {MODE_GRAPH, G_SCRUB_START, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_GRAPH, G_SCRUB, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_GRAPH, G_SCRUB_END, ZONE_ANY, WHEN_ALWAYS, NAV_SCRUB},
    {MODE_GRAPH, G_LONG_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_GRAPH, G_SWIPE_RIGHT, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK},
    {MODE_GRAPH, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_NEXT}, /* one row older */
    {MODE_GRAPH, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_GRAPH, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
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
    static const char *const names[MODE_COUNT] = {"home", "agent", "ask", "scroll", "status", "watch", "voice", "keys", "read", "labs", "trackpad", "graph"};
    return (unsigned)m < MODE_COUNT ? names[m] : "?";
}

const char *nav_action_name(nav_action_t a)
{
    static const char *const names[NAV_COUNT] = {
        "none", "focus", "open_agent", "open_ask", "open_status", "back", "home", "next", "prev", "next_attention", "pick_rim",
        "scrub", "commit", "hold_press", "hold_release", "hold_long", "scroll_drag", "scroll_fling",
        "scroll_catch", "page_newer", "to_bottom", "erase", "open_labs", "pointer"};
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

int nav_scroll_lines(int dy_px)
{
    return dy_px / GESTURE_DRAG_STEP_PX * NAV_SCROLL_LINES_PER_STEP;
}

int nav_agent_items(sess_state_t state, bool typing, bool replies, bool queue_ok, bool queued, bool message, bool graph,
                    agent_item_t *out, int cap)
{
    /* UI-V2 §4.2: Focus is button A on HOME and WATCH, Status the swipe down on HOME, so neither takes
     * a row; a session that waits for input keeps Focus. Quick replies and Interrupt need a daemon that
     * types into terminals (issue b150). */
    agent_item_t list[8];
    int n = 0;
    bool reply = typing && replies;
#define ADD(id, hold) list[n++] = (agent_item_t){(id), (hold)}
    /* what the agent said comes first once it has finished (LAST-MESSAGE.md): it is why the user looks */
    if (message && state != SESS_START && state != SESS_WORK) ADD(AG_READ, false);
    ADD(AG_WATCH, false); /* first otherwise: tap, tap opens the monitor, a third tap focuses */
    if (graph) ADD(AG_GRAPH, false);
    switch (state) {
    case SESS_DONE:
        if (reply) ADD(AG_REPLY, false);
        break;
    case SESS_ERROR:
        ADD(AG_DISMISS, false);
        if (reply) ADD(AG_REPLY, false);
        break;
    case SESS_IDLE:
        if (reply) ADD(AG_REPLY, false);
        ADD(AG_SNOOZE, false);
        break;
    case SESS_INPUT:
        if (reply) ADD(AG_REPLY, false);
        ADD(AG_FOCUS, false);
        break;
    default:
        ADD(AG_NOTIFY, false);
        ADD(AG_SNOOZE, false);
        if (queue_ok) ADD(AG_QUEUE, false);
        if (queued) ADD(AG_UNQUEUE, false);
        if (typing) ADD(AG_INTERRUPT, true);
        queued = false;
        break;
    }
    if (queued) ADD(AG_UNQUEUE, false);
#undef ADD
    int k = 0;
    for (int i = 0; i < n && k < cap; i++) out[k++] = list[i];
    return k;
}

int nav_job_items(sess_state_t state, agent_item_t *out, int cap)
{
    int n = 0;
    if (state == SESS_START || state == SESS_WORK) {
        if (n < cap) out[n++] = (agent_item_t){AG_INTERRUPT, true};
    } else {
        if ((state == SESS_DONE || state == SESS_ERROR) && n < cap) out[n++] = (agent_item_t){AG_RERUN, true};
        if (n < cap) out[n++] = (agent_item_t){AG_DISMISS, false};
    }
    return n;
}

int nav_ask_items(const attention_t *a, bool voice, ask_item_t *out, int cap)
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
        /* third, two steps from the default, so the plain Approve stays the one under the finger */
        if (a->has_rule && (a->acts & (1u << ACT_APPROVE)) && n < cap - 1)
            out[n++] = (ask_item_t){ASK_GRANT, -1, NAV_GRANT_HOLD_MS, true};
    }
    /* opens VOICE on this request; the hold-to-send there is the commit */
    if (voice && (a->acts & (1u << ACT_DICTATE)) && n < cap - 1) out[n++] = (ask_item_t){ASK_DICTATE, -1, 0, false};
    out[n++] = (ask_item_t){ASK_OPEN_PC, -1, 0, false}; /* always: the escape to the terminal */
    return n;
}

int nav_question_items(const ask_q_t *q, bool voice, uint32_t picks, ask_item_t *out, int cap)
{
    int n = 0;
    if (!q || cap < 1) return 0;
    for (int i = 0; i < q->n_opts && i < MODEL_MAX_QOPTS && n < cap - 1; i++)
        out[n++] = (ask_item_t){ASK_QOPT, i, q->multi ? 0 : NAV_HOLD_MS, false};
    if (voice && n < cap - 1) out[n++] = (ask_item_t){ASK_OTHER, -1, 0, false}; /* opens VOICE; its own hold commits */
    (void)picks; /* Send stays put while ticks change, so the list never jumps under the finger */
    if (q->multi && n < cap - 1) out[n++] = (ask_item_t){ASK_SEND, -1, NAV_HOLD_MS, false};
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

int nav_rim_scrub(int *last, gesture_kind_t kind, int sector, int sectors)
{
    if (kind == G_SCRUB_START) {
        *last = sector;
        return 0;
    }
    if (kind != G_SCRUB) return 0;
    int d = sector - *last;
    if (d > sectors / 2) d -= sectors;
    else if (d < -sectors / 2) d += sectors;
    *last = sector;
    return d;
}

int nav_list_scrub(int *last, gesture_kind_t kind, int sector)
{
    return nav_rim_scrub(last, kind, sector, NAV_LIST_SECTORS);
}

int nav_list_drag(int *acc, int dy)
{
    *acc += dy;
    int n = -(*acc / NAV_LIST_DRAG_PX); /* C division truncates toward zero, so both directions keep a remainder */
    *acc += n * NAV_LIST_DRAG_PX;
    return n;
}

int nav_pointer_gain10(int dx, int dy)
{
    int step = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    int g = 12 + step * 8 / 10; /* 1.2 + 0.08 per px of the step */
    return g > 40 ? 40 : g;
}
