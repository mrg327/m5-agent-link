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
    {MODE_HOME, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_REQUEST, NAV_DENY}, /* STATUS later, when no request */
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
    /* SCROLL: the centre's vertical motion drives the terminal (UI-V2 §4.4). The rim is unused, so
     * nothing here is a scrub, and swipe down means a page, never deny or back. */
    {MODE_SCROLL, G_DRAG, ZONE_CENTRE, WHEN_ALWAYS, NAV_SCROLL_DRAG},
    {MODE_SCROLL, G_SWIPE_UP, ZONE_CENTRE, WHEN_ALWAYS, NAV_PAGE_OLDER},   /* only a flick counts */
    {MODE_SCROLL, G_SWIPE_DOWN, ZONE_CENTRE, WHEN_ALWAYS, NAV_PAGE_NEWER}, /* only a flick counts */
    {MODE_SCROLL, G_TAP, ZONE_CENTRE, WHEN_ALWAYS, NAV_BACK}, /* leaves the terminal where it is */
    {MODE_SCROLL, G_BTN_A, ZONE_ANY, WHEN_ALWAYS, NAV_TO_BOTTOM},
    {MODE_SCROLL, G_BTN_B, ZONE_ANY, WHEN_ALWAYS, NAV_PAGE_NEWER},
    {MODE_SCROLL, G_BTN_B_LONG, ZONE_ANY, WHEN_ALWAYS, NAV_BACK},
    {MODE_SCROLL, G_CHORD, ZONE_ANY, WHEN_ALWAYS, NAV_HOME},
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
    static const char *const names[MODE_COUNT] = {"home", "agent", "ask", "scroll"};
    return (unsigned)m < MODE_COUNT ? names[m] : "?";
}

const char *nav_action_name(nav_action_t a)
{
    static const char *const names[NAV_COUNT] = {
        "none", "focus", "open_agent", "open_ask", "back", "home", "next", "prev", "next_attention", "pick_rim",
        "scrub", "commit", "deny", "hold_press", "hold_release", "hold_long", "scroll_drag", "page_older",
        "page_newer", "to_bottom"};
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

int nav_agent_items(sess_state_t state, bool scroll_ok, agent_item_t *out, int cap)
{
    static const agent_item_t work[] = {{AG_FOCUS, false}, {AG_SCROLL, false}, {AG_SNOOZE, false}, {AG_NOTIFY, false}, {AG_WATCH, false}};
    static const agent_item_t done[] = {{AG_FOCUS_ACK, false}, {AG_ACK, false}, {AG_SCROLL, false}, {AG_WATCH, false}};
    static const agent_item_t error[] = {{AG_FOCUS, false}, {AG_DISMISS, false}, {AG_SCROLL, false}, {AG_WATCH, false}};
    static const agent_item_t idle[] = {{AG_FOCUS, false}, {AG_SNOOZE, false}, {AG_WATCH, false}};
    static const agent_item_t input[] = {{AG_FOCUS, false}, {AG_SCROLL, false}, {AG_WATCH, false}}; /* ASK takes over in Phase 3 */
    const agent_item_t *src;
    int n;
    switch (state) {
    case SESS_DONE: src = done; n = 4; break;
    case SESS_ERROR: src = error; n = 4; break;
    case SESS_IDLE: src = idle; n = 3; break;
    case SESS_INPUT: src = input; n = 3; break;
    default: src = work; n = 5; break;
    }
    int k = 0;
    for (int i = 0; i < n && k < cap; i++) {
        if (src[i].id == AG_SCROLL && !scroll_ok) continue;
        out[k++] = src[i];
    }
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
