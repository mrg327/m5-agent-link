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

    printf("%s\n", failures ? "nav: FAILED" : "nav: ok");
    return failures ? 1 : 0;
}
