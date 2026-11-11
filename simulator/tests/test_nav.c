/* nav.c interaction model: the ASK item list. */
#include <stdio.h>
#include <string.h>

#include "nav.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

static attention_t question(int n_opts, uint8_t acts)
{
    attention_t a;
    memset(&a, 0, sizeof a);
    a.rid = 8;
    a.sid = 1;
    a.kind = ATT_QUESTION;
    a.n_opts = (uint8_t)n_opts;
    a.acts = acts;
    return a;
}

int main(void)
{
    ask_item_t it[ASK_MAX_ITEMS];
    uint8_t dictate = (1u << ACT_FOCUS) | (1u << ACT_DICTATE);

    /* a free-text field: no options, Dictate... then Open on PC; opening VOICE is not a commit */
    attention_t a = question(0, dictate);
    int n = nav_ask_items(&a, true, it, ASK_MAX_ITEMS);
    EXPECT(n == 2 && it[0].id == ASK_DICTATE && it[1].id == ASK_OPEN_PC, "free text: Dictate..., Open on PC");
    EXPECT(it[0].hold_ms == 0 && !it[0].grants, "Dictate... commits by a tap");

    /* no voice from the daemon: only the escape to the PC */
    n = nav_ask_items(&a, false, it, ASK_MAX_ITEMS);
    EXPECT(n == 1 && it[0].id == ASK_OPEN_PC, "no voice: no Dictate...");

    /* voice, but the request does not take dictation */
    a = question(0, 1u << ACT_FOCUS);
    n = nav_ask_items(&a, true, it, ASK_MAX_ITEMS);
    EXPECT(n == 1 && it[0].id == ASK_OPEN_PC, "no dictate act: no Dictate...");

    /* options first, then Dictate..., Open on PC last */
    a = question(4, dictate | (1u << ACT_REPLY));
    n = nav_ask_items(&a, true, it, ASK_MAX_ITEMS);
    EXPECT(n == 6, "4 options + Dictate... + Open on PC");
    for (int i = 0; i < 4 && i < n; i++) EXPECT(it[i].id == ASK_OPT && it[i].opt == i, "options in order");
    EXPECT(n == 6 && it[4].id == ASK_DICTATE && it[5].id == ASK_OPEN_PC, "Dictate... before Open on PC");

    /* the escape always fits, even when the list is cut short */
    n = nav_ask_items(&a, true, it, 3);
    EXPECT(n == 3 && it[2].id == ASK_OPEN_PC, "short list keeps Open on PC last");

    /* UI-V3 §5: a swipe right in the centre goes back from every deeper mode except KEYS, never deny */
    static const nav_mode_t deeper[] = {MODE_AGENT, MODE_ASK, MODE_WATCH, MODE_SCROLL, MODE_STATUS, MODE_VOICE};
    for (unsigned i = 0; i < sizeof deeper / sizeof deeper[0]; i++) {
        nav_mode_t m = deeper[i];
        for (int req = 0; req < 2; req++)
            EXPECT(nav_lookup(m, G_SWIPE_RIGHT, ZONE_CENTRE, req, req) == NAV_BACK, "swipe right in the centre goes back");
        EXPECT(nav_lookup(m, G_SWIPE_RIGHT, ZONE_RIM, false, false) != NAV_BACK, "a rim swipe is not back");
    }
    EXPECT(nav_lookup(MODE_ASK, G_SWIPE_RIGHT, ZONE_CENTRE, true, true) == NAV_BACK, "swipe right is back, never an answer");
    EXPECT(nav_lookup(MODE_ASK, G_SWIPE_DOWN, ZONE_CENTRE, true, true) == NAV_NONE, "no swipe denies: deny is only its hold");
    EXPECT(nav_lookup(MODE_AGENT, G_SWIPE_DOWN, ZONE_CENTRE, false, false) == NAV_NONE, "swipe down no longer goes back: the centre drag moves the list");
    EXPECT(nav_lookup(MODE_HOME, G_SWIPE_RIGHT, ZONE_CENTRE, false, false) == NAV_NONE, "HOME has no swipe-right binding");
    {
        /* a job's menu (JOBS.md): Stop while it runs, Run again and Dismiss after; both actions need a hold */
        agent_item_t it[4];
        EXPECT(nav_job_items(SESS_WORK, it, 4) == 1 && it[0].id == AG_INTERRUPT && it[0].hold, "a running job: Stop, by a hold");
        EXPECT(nav_job_items(SESS_ERROR, it, 4) == 2 && it[0].id == AG_RERUN && it[0].hold && it[1].id == AG_DISMISS && !it[1].hold,
               "a failed job: Run again by a hold, Dismiss by a tap");
        EXPECT(nav_job_items(SESS_DONE, it, 4) == 2 && it[0].id == AG_RERUN && it[0].hold, "a finished job: Run again by a hold");
        EXPECT(nav_job_items(SESS_IDLE, it, 4) == 1 && it[0].id == AG_DISMISS, "anything else can only be dismissed");
    }
    EXPECT(nav_lookup(MODE_KEYS, G_SWIPE_RIGHT, ZONE_CENTRE, false, false) == NAV_NONE, "KEYS keeps touch for strokes");

    printf("%s\n", failures ? "nav: FAILED" : "nav: ok");
    return failures ? 1 : 0;
}
