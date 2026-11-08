#include "nav.h"

#include <stddef.h>

/* UI-V2 §3, one row per cell. Order matters only where two rows share a gesture and zone. */
const nav_binding_t nav_bindings[] = {
    /* HOME: the deck */
    {MODE_HOME, G_PRESS, ZONE_CENTRE, WHEN_ALWAYS, NAV_HOLD_PRESS},
    {MODE_HOME, G_RELEASE, ZONE_ANY, WHEN_ALWAYS, NAV_HOLD_RELEASE},
    {MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_REQUEST, NAV_FOCUS}, /* review on the PC: a tap never approves */
    {MODE_HOME, G_TAP, ZONE_CENTRE, WHEN_NO_REQUEST, NAV_OPEN_AGENT},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_REQUEST, NAV_FOCUS},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_CENTRE, WHEN_NO_REQUEST, NAV_OPEN_AGENT},
    {MODE_HOME, G_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_HOME, G_DOUBLE_TAP, ZONE_RIM, WHEN_ALWAYS, NAV_PICK_RIM},
    {MODE_HOME, G_LONG_PRESS, ZONE_CENTRE, WHEN_REQUEST, NAV_HOLD_LONG}, /* otherwise reserved */
    {MODE_HOME, G_SWIPE_UP, ZONE_CENTRE, WHEN_ALWAYS, NAV_OPEN_AGENT},
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
};
const int nav_n_bindings = (int)(sizeof nav_bindings / sizeof nav_bindings[0]);

nav_action_t nav_lookup(nav_mode_t mode, gesture_kind_t g, nav_zone_t zone, bool request_pending)
{
    for (int i = 0; i < nav_n_bindings; i++) {
        const nav_binding_t *b = &nav_bindings[i];
        if (b->mode != mode || b->gesture != g) continue;
        if (b->zone != ZONE_ANY && b->zone != zone) continue;
        if (b->when == WHEN_REQUEST && !request_pending) continue;
        if (b->when == WHEN_NO_REQUEST && request_pending) continue;
        return b->action;
    }
    return NAV_NONE;
}

const char *nav_mode_name(nav_mode_t m)
{
    static const char *const names[MODE_COUNT] = {"home", "agent"};
    return (unsigned)m < MODE_COUNT ? names[m] : "?";
}

const char *nav_action_name(nav_action_t a)
{
    static const char *const names[NAV_COUNT] = {
        "none", "focus", "open_agent", "back", "home", "next", "prev", "next_attention", "pick_rim",
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
    if (n > cap) n = cap;
    for (int i = 0; i < n; i++) out[i] = src[i];
    return n;
}
