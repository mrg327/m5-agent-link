/* Interaction model (docs/design/UI-V2.md §3 and "Architecture"): modes, the gesture → action table,
 * the safety rules and the per-state action lists. Pure C, no LVGL: a reskin never touches this file,
 * and a gesture remap never touches a drawing file. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "gesture.h"
#include "model.h"

typedef enum { MODE_HOME, MODE_AGENT, MODE_ASK, MODE_SCROLL, MODE_STATUS, MODE_WATCH, MODE_VOICE, MODE_KEYS, MODE_READ, MODE_COUNT } nav_mode_t;

/* Where a touch gesture happened, by its press (or swipe start) point. */
typedef enum { ZONE_ANY, ZONE_CENTRE, ZONE_RIM } nav_zone_t;

/* Condition on the selected session. REQUEST: an unanswered *approvable* permission (approve in
 * acts) is on screen, the only state in which deny exists. ASKABLE: any unanswered permission or
 * question, i.e. there is something ASK can show. REQUEST implies ASKABLE. */
typedef enum { WHEN_ALWAYS, WHEN_REQUEST, WHEN_NO_REQUEST, WHEN_ASKABLE, WHEN_NOT_ASKABLE } nav_when_t;

typedef enum {
    NAV_NONE,
    NAV_FOCUS,          /* default action: focus the terminal (Done also acknowledges) */
    NAV_OPEN_AGENT,     /* HOME → AGENT for the selected session */
    NAV_OPEN_ASK,       /* HOME → ASK: the pending request with its options; opening never answers */
    NAV_OPEN_STATUS,    /* HOME → STATUS (swipe down): link, battery, quiet mode */
    NAV_BACK,           /* leave the current mode for HOME; a swipe down must be a long one, a swipe right any */
    NAV_HOME,           /* A+B chord: straight back to HOME */
    NAV_NEXT,           /* next session (HOME) or next item (AGENT) */
    NAV_PREV,
    NAV_NEXT_ATTENTION, /* jump to the next session that needs you */
    NAV_PICK_RIM,       /* tap on the rim: select the segment or item under the finger */
    NAV_SCRUB,          /* rim drag; the gesture kind says start / move / end */
    NAV_COMMIT,         /* AGENT: commit the selected item (safe items only; hold items need a hold) */
    NAV_HOLD_PRESS,     /* raw finger down in the centre: may arm a hold */
    NAV_HOLD_RELEASE,   /* raw finger up: cancels a hold in progress */
    NAV_HOLD_LONG,      /* a long press whose duration already covers the hold (injected input) */
    NAV_SCROLL_DRAG,    /* SCROLL: a vertical drag step; dy → lines (up = older) */
    NAV_SCROLL_FLING,   /* SCROLL: the finger lifted off a drag; vy may start the glide */
    NAV_SCROLL_CATCH,   /* SCROLL: any new touch stops the glide */
    NAV_PAGE_NEWER,     /* SCROLL: a page toward newer output (button B) */
    NAV_TO_BOTTOM,      /* SCROLL: button A: back to the live bottom, then leave */
    NAV_ERASE,          /* KEYS: button B: delete the last typed word */
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

nav_action_t nav_lookup(nav_mode_t mode, gesture_kind_t g, nav_zone_t zone, bool request_pending, bool askable);
const char *nav_mode_name(nav_mode_t m);
const char *nav_action_name(nav_action_t a);

/* ── safety rules (UI-V2 §1): structural, so every skin keeps them ── */
#define NAV_CENTRE_R 150     /* a touch inside this radius is a centre touch; outside it is the rim */
#define NAV_HOLD_MS 1200     /* hold-to-approve duration; the daemon's floor is 1000 ms */
#define NAV_DENY_HOLD_MS 800 /* hold-to-deny: shorter, since a deny only sends the agent back to the prompt */
#define NAV_GRANT_HOLD_MS 1600 /* hold for "Always allow" (STANDING-RULES.md): longer than approve; the daemon's floor is 1500 ms */
#define NAV_HOLD_SHOW_MS 250 /* the arc and the haptic ramp only start after this, so a tap never flashes them */
#define NAV_LIST_SECTORS 9   /* a rim drag in a list is a dial: one item per 40 degrees, clockwise = next */
#define NAV_LIST_DRAG_PX 40  /* a vertical drag in the centre of a list: one item per 40 px (the first also pays the 18 px tap slop) */
#define NAV_CARD_ARM_MS 300  /* a hold only starts once the request has been on screen this long */
#define NAV_LONG_SWIPE_PX 120 /* deny, and back-from-AGENT: a deliberate swipe, at least this long */
#define NAV_AGENT_TIMEOUT_MS 8000
#define NAV_SCROLL_TIMEOUT_MS 8000 /* SCROLL closes this long after the last input */
#define NAV_STATUS_TIMEOUT_MS 8000 /* STATUS closes this long after the last input */
/* WATCH has no timeout: it monitors until the user leaves (UI-V2 §4.7). */

/* ── SCROLL (UI-V2 §4.4, §6): the device side of cmd_op_scroll ── */
typedef enum { SCROLL_LINES = 0, SCROLL_PAGES = 1, SCROLL_TO_END = 2 } nav_scroll_unit_t; /* wire values */
#define NAV_SCROLL_LINES_PER_STEP 1 /* one drag step (GESTURE_DRAG_STEP_PX) is one wheel line */
#define NAV_SCROLL_COALESCE_MS 40   /* at most one command per window (≤ 25 cmds/s); an idle link sends at once */
#define NAV_SCROLL_DETENT_LINES 3   /* one detent per this many finger-driven lines, like a wheel notch */
/* Glide after release (a trackpad's momentum): starts at this release speed, decays exponentially. */
#define NAV_GLIDE_MIN_START 300 /* px/s */
#define NAV_GLIDE_MAX 3000      /* px/s: a faster release glides at this */
#define NAV_GLIDE_STOP 60       /* px/s: slower than this, the glide ends */
#define NAV_GLIDE_TAU_MS 400    /* velocity time constant */
#define NAV_GLIDE_CAP_MS 2500   /* hard limit on one glide */
#define NAV_SCROLL_MAX_LINES 500    /* per command, as the daemon clamps */
#define NAV_SCROLL_MAX_PAGES 20
/* Lines for a drag step: negative = up = older, like the wire delta. */
int nav_scroll_lines(int dy_px);

bool nav_centre(int x, int y);
/* A hold may start only for the request that is on screen, and only once it has been there NAV_CARD_ARM_MS. */
bool nav_hold_may_start(uint32_t shown_rid, uint32_t shown_since_ms, uint32_t rid, uint32_t now_ms);
/* A downward swipe that started above the centre line and travelled at least NAV_LONG_SWIPE_PX. */
bool nav_long_swipe_down(int y_start, int y_end);

/* ── AGENT: which actions a session offers, in wheel order, default first (UI-V2 §4.2) ── */
typedef enum {
    AG_FOCUS, AG_DISMISS, AG_SNOOZE, AG_NOTIFY, AG_WATCH, AG_REPLY, AG_INTERRUPT, AG_RERUN, AG_QUEUE, AG_UNQUEUE, AG_READ, AG_COUNT
} agent_action_t;

typedef struct {
    agent_action_t id;
    bool hold; /* destructive: commits only by a hold, never by a tap or button A */
} agent_item_t;

/* Few items: anything with another path (Focus on button A, Status on swipe down) stays off the wheel.
 * typing: the daemon can type into terminals (welcome.replies present): Interrupt on a working
 * session (a hold), and "Reply..." where the agent waits for you when there are replies to offer. */
/* `queue_ok`: a reply can be queued (daemon proto >= 0.17 and quick replies or voice); `queued`: the
 * session holds a queued message, which any state can cancel; `message`: the agent's last message can be read
 * (proto 0.18, LAST-MESSAGE.md): "Read" leads a finished session's list. */
int nav_agent_items(sess_state_t state, bool typing, bool replies, bool queue_ok, bool queued, bool message, agent_item_t *out,
                    int cap);
/* A job's menu (JOBS.md §5): Stop while it runs; Run again and Dismiss once it ended. Stop and Run again are
 * hold items: one interrupts a process, the other starts one. */
int nav_job_items(sess_state_t state, agent_item_t *out, int cap);

/* ── ASK: the answer options of a request, in wheel order, default first (UI-V2 §4.3, §7) ──
 * Options come from attention.opts verbatim; a bare permission synthesises Approve / Deny; "Open on
 * PC" is always last, after "Dictate..." where the request takes free text (acts has ACT_DICTATE) and
 * the daemon transcribes (voice). Anything that grants permission (grant_mask bit, or Approve) commits only by
 * the NAV_HOLD_MS hold; a deny-class option by the NAV_DENY_HOLD_MS hold (or the strict swipe); a
 * question option by a tap. A bare permission whose attention carries a standing rule (has_rule, proto 0.13)
 * also offers "Always allow" after Deny: a grant item held for NAV_GRANT_HOLD_MS (STANDING-RULES.md). */
typedef enum { ASK_OPT, ASK_APPROVE, ASK_DENY, ASK_DICTATE, ASK_OPEN_PC, ASK_GRANT, ASK_QOPT, ASK_OTHER, ASK_SEND } ask_action_t;

typedef struct {
    ask_action_t id;
    int opt;          /* ASK_OPT: index into attention.opts (the cmd_op_reply argument) */
    uint32_t hold_ms; /* 0: commits by tap or button A; else only by a hold of this length */
    bool grants;      /* approve-class: hold arc and haptic ramp use the approve pattern */
} ask_item_t;

#define ASK_MAX_ITEMS 7 /* 4 options + Approve + Deny + Dictate + Open on PC never all coexist, but keep room */
int nav_ask_items(const attention_t *a, bool voice, ask_item_t *out, int cap);
/* One question of a multiple-choice request (proto 0.18, LAST-MESSAGE.md §3.3), in wheel order: its options
 * (ASK_QOPT, opt = index), then "Other" (ASK_OTHER, a dictated answer) when the daemon transcribes, then for a
 * multi-select question "Send" / "Next" (ASK_SEND), then Open on PC. A
 * single-choice option is picked only by the NAV_HOLD_MS hold; a multi-select option is ticked by a tap; Send is a
 * hold, refused while nothing is ticked (it is listed regardless, so the list never jumps). Nothing here grants
 * permission. */
int nav_question_items(const ask_q_t *q, bool voice, uint32_t picks, ask_item_t *out, int cap);

/* ── STATUS: the watch's own rows, in wheel order (UI-V2 §4.5) ──
 * Quiet first: it is what the screen is for. Rows that are not actionable only inform; committing
 * them does nothing. Quiet mode is device-side and timed: Off → 30 min → 1 h → Off. While it runs,
 * attention arrivals neither buzz, glow nor nag; the card still updates and ASK still opens when the
 * display is already on. The alert mode (Sounds) toggles between full and quiet (sound off). */
typedef enum { ST_QUIET, ST_SOUNDS, ST_PRIVACY, ST_SESSIONS, ST_FIRMWARE, ST_COUNT } status_row_t;

typedef struct {
    status_row_t id;
    bool actionable;
} status_item_t;

int nav_status_items(status_item_t *out, int cap);
#define NAV_QUIET_STEP1_S (30u * 60u)
#define NAV_QUIET_STEP2_S (60u * 60u)
/* The next quiet_until after a commit on the Quiet row: 0 (off) → now + 30 min → now + 1 h → 0. */
uint32_t nav_quiet_next(uint32_t quiet_until, uint32_t now_unix);
/* Which step nav_quiet_next() would take: 1 = 30 min, 2 = 1 h, 0 = off. */
int nav_quiet_step(uint32_t quiet_until, uint32_t now_unix);
/* A rim drag in a list (AGENT, ASK, STATUS) steps through the items like a dial, wherever on the rim the
 * finger is: the list is not drawn on the ring, so the finger's absolute angle means nothing. The screen sets
 * NAV_LIST_SECTORS rim sectors on opening; this returns how many items to move for one scrub event (clockwise
 * positive) and keeps the last sector in *last. */
int nav_list_scrub(int *last, gesture_kind_t kind, int sector);
/* A vertical drag in the centre of a list moves the list with the finger: dragging up brings the next item
 * in. Feed each G_DRAG step's dy; returns the items to move (next positive) and keeps the remainder in *acc. */
int nav_list_drag(int *acc, int dy);

