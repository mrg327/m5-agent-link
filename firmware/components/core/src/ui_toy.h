/* Toys (EXPERIMENTS.md "Toys"): small things to play with, opened from LABS and hosted by one mode,
 * MODE_TOY. A toy is one `ui_<toy>.c` (drawing and input) over a pure `<toy>.c` (its state), and a
 * row in s_builtin (ui_toy.c); it adds nothing to nav.c, ui.c or enter_mode().
 *
 * The contract every toy keeps, so an agent always comes first:
 * - A request or an error takes the screen at once: the host calls pause(), the toy keeps all its
 *   state (static), and after the answer the watch comes back to it, paused. Only open() restarts
 *   the screen; a toy resumes on a tap or A, never by itself.
 * - It buzzes and sounds only through g_ui_actions->toy_buzz / toy_detent / toy_play (the toy gate:
 *   never over an alert, never where alerts would not, feedback.h), and its microphone through
 *   toy_mic. Its sounds live in the toy sound block (sound.h, sound_toy_pcm), not the alert cues.
 * - A timer obeys the alert mode as alerts do (full: buzz and cue; quiet: buzz; silent: neither);
 *   the timed Quiet of STATUS is for agents and does not hold it back.
 * - It never uses A+B together (the chord is Home) and leaves by B held, a swipe down or right
 *   (unless it says otherwise in EXPERIMENTS.md). */
#pragma once
#include "ui_internal.h"

typedef struct {
    const char *name;  /* the report and the log */
    const char *glyph; /* its disc in LABS (ui_icons.h) */
    /* key edges at once (G_KEY_DOWN / G_KEY_UP, no GESTURE_BTN_DOUBLE_MS wait), act gets
     * NAV_TOY_KEY; off: A and B arrive as G_BTN_A (NAV_COMMIT) and G_BTN_B (NAV_NEXT) */
    bool raw_keys;
    /* it could sway an answer (a die, an oracle): LABS refuses to open it while a request waits */
    bool not_over_request;
    void (*init)(
        lv_obj_t *root); /* once: build its objects on root (the host shows and hides it) */
    void (*open)(uint32_t now_ms);  /* on screen: first time, or back after a pause */
    void (*pause)(uint32_t now_ms); /* off screen (a takeover, back, the chord): stop, keep state */
    /* every gesture on its screen, with the nav action (NAV_NONE for raw edges such as G_PRESS);
     * true when used */
    bool (*act)(nav_action_t a, const gesture_evt_t *g);
    void (*tick)(uint32_t now_ms);  /* every step while open */
    void (*report)(ui_report_t *r); /* labels in words, for the debug channel and scripts */
} ui_toy_t;

#define UI_TOY_MAX 8

/* Adds a toy (the built-in ones at ui_init; the simulator's stub from a script). Returns its index,
 * -1 when the table is full. */
int ui_toy_register(const ui_toy_t *toy);
int ui_toy_count(void);
const ui_toy_t *ui_toy_get(int i);
/* The toy that was left paused (a takeover or leaving), -1 when none: LABS opens on it. */
int ui_toy_paused(void);
/* What the fun word lets toys do now (persona_allow(PF_TOYS), the user starting it): 0 when the
 * toys are off, so LABS lists none; without PA_ONESHOT (reduced motion) a toy shows end frames. */
unsigned ui_toy_allow(void);

/* The host (ui.c). */
lv_obj_t *ui_toy_init(lv_obj_t *screen);
void ui_toy_select(int i); /* the toy MODE_TOY opens next */
void ui_toy_open(uint32_t now_ms);
void ui_toy_close(void);
bool ui_toy_act(nav_action_t a, const gesture_evt_t *g);
void ui_toy_tick(uint32_t now_ms);
void ui_toy_report(ui_report_t *r);
void ui_go_toy(int i); /* LABS: open toy i (ui.c) */
