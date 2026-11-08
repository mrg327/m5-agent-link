/* Interaction model (docs/design/UI-V2.md §3 and "Architecture"): modes, the gesture → action table,
 * the safety rules and the per-state action lists. Pure C, no LVGL: a reskin never touches this file,
 * and a gesture remap never touches a drawing file. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "gesture.h"
#include "model.h"

typedef enum { MODE_HOME, MODE_AGENT, MODE_COUNT } nav_mode_t;

/* Where a touch gesture happened, by its press (or swipe start) point. */
typedef enum { ZONE_ANY, ZONE_CENTRE, ZONE_RIM } nav_zone_t;

/* Condition on the selected session: does it have an unanswered approvable request on screen? */
typedef enum { WHEN_ALWAYS, WHEN_REQUEST, WHEN_NO_REQUEST } nav_when_t;

typedef enum {
    NAV_NONE,
    NAV_FOCUS,          /* default action: focus the terminal (Done also acknowledges) */
    NAV_OPEN_AGENT,     /* HOME → AGENT for the selected session */
    NAV_BACK,           /* leave the current mode for HOME; from a swipe it must be a long one */
    NAV_HOME,           /* A+B chord: straight back to HOME */
    NAV_NEXT,           /* next session (HOME) or next item (AGENT) */
    NAV_PREV,
    NAV_NEXT_ATTENTION, /* jump to the next session that needs you */
    NAV_PICK_RIM,       /* tap on the rim: select the segment or item under the finger */
    NAV_SCRUB,          /* rim drag; the gesture kind says start / move / end */
    NAV_COMMIT,         /* AGENT: commit the selected item (safe items only; hold items need a hold) */
    NAV_DENY,           /* HOME with a request: the strict deny swipe */
    NAV_HOLD_PRESS,     /* raw finger down in the centre: may arm a hold */
    NAV_HOLD_RELEASE,   /* raw finger up: cancels a hold in progress */
    NAV_HOLD_LONG,      /* a long press whose duration already covers the hold (injected input) */
    NAV_COUNT
} nav_action_t;

typedef struct {
    nav_mode_t mode;
    gesture_kind_t gesture;
    nav_zone_t zone;
    nav_when_t when;
    nav_action_t action;
} nav_binding_t;

/* The one gesture map. Rows are matched in order; the first fit wins. */
extern const nav_binding_t nav_bindings[];
extern const int nav_n_bindings;

nav_action_t nav_lookup(nav_mode_t mode, gesture_kind_t g, nav_zone_t zone, bool request_pending);
const char *nav_mode_name(nav_mode_t m);
const char *nav_action_name(nav_action_t a);

/* ── safety rules (UI-V2 §1): structural, so every skin keeps them ── */
#define NAV_CENTRE_R 150     /* a touch inside this radius is a centre touch; outside it is the rim */
#define NAV_HOLD_MS 1200     /* hold-to-approve duration; the daemon's floor is 1000 ms */
#define NAV_HOLD_SHOW_MS 250 /* the arc and the haptic ramp only start after this, so a tap never flashes them */
#define NAV_CARD_ARM_MS 300  /* a hold only starts once the request has been on screen this long */
#define NAV_LONG_SWIPE_PX 120 /* deny, and back-from-AGENT: a deliberate swipe, at least this long */
#define NAV_AGENT_TIMEOUT_MS 8000

bool nav_centre(int x, int y);
/* A hold may start only for the request that is on screen, and only once it has been there NAV_CARD_ARM_MS. */
bool nav_hold_may_start(uint32_t shown_rid, uint32_t shown_since_ms, uint32_t rid, uint32_t now_ms);
/* A downward swipe that started above the centre line and travelled at least NAV_LONG_SWIPE_PX. */
bool nav_long_swipe_down(int y_start, int y_end);

/* ── AGENT: which actions a session offers, in wheel order, default first (UI-V2 §4.2) ── */
typedef enum {
    AG_FOCUS, AG_FOCUS_ACK, AG_ACK, AG_DISMISS, AG_SCROLL, AG_SNOOZE, AG_NOTIFY, AG_WATCH, AG_COUNT
} agent_action_t;

typedef struct {
    agent_action_t id;
    bool hold; /* destructive: commits only by a hold, never by a tap or button A */
} agent_item_t;

int nav_agent_items(sess_state_t state, agent_item_t *out, int cap);
