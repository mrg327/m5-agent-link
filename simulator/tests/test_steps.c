#include <stdio.h>
#include <string.h>

#include "steps.h"

static int failures;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } } while (0)

int main(void)
{
    steps_t t;
    steps_clear(&t);

    steps_update(&t, true, 1, false, false, "Read · a.c");
    EXPECT(t.count == 1 && t.s[0].state == STEP_RUNNING, "n=1 starts a running step");
    EXPECT(!strcmp(t.s[0].text, "Read · a.c"), "step text kept");

    steps_update(&t, false, 0, true, true, NULL);
    EXPECT(t.s[0].state == STEP_OK, "ok marks the current step");

    steps_update(&t, true, 2, false, false, "Edit · b.c");
    steps_update(&t, true, 2, true, false, NULL);
    EXPECT(t.count == 2 && t.s[1].state == STEP_FAIL && t.s[0].state == STEP_OK, "fail marks only step 2");

    steps_update(&t, true, 3, false, false, "Bash · make");
    steps_update(&t, true, 4, false, false, "Bash · ctest");
    steps_update(&t, true, 5, false, false, "Grep · x");
    EXPECT(t.count == STEPS_MAX && t.s[0].n == 2 && t.s[3].n == 5, "keeps the last four, oldest drops");
    EXPECT(steps_find(&t, 1) == NULL && steps_find(&t, 4) != NULL, "find by n");

    steps_update(&t, true, 5, false, false, "Grep · y");
    EXPECT(t.count == STEPS_MAX && !strcmp(t.s[3].text, "Grep · y"), "same n updates the running text only");
    steps_update(&t, true, 5, true, true, NULL);
    steps_update(&t, false, 0, false, false, "Grep · z");
    EXPECT(!strcmp(t.s[3].text, "Grep · y"), "a finished step keeps its text");

    steps_update(&t, true, 1, false, false, "Read · new");
    EXPECT(t.count == 1 && t.s[0].n == 1 && t.s[0].state == STEP_RUNNING, "n decreasing starts a new turn");
    steps_update(&t, true, 0, false, false, NULL);
    EXPECT(t.count == 0 && t.last_n == 0, "n = 0 clears");

    steps_update(&t, true, 1, false, false, "A very long step text that goes well past the limit of forty chars · é");
    EXPECT(strlen(t.s[0].text) < STEP_TEXT_MAX, "text is bounded");

    if (failures) return 1;
    printf("test_steps: ok\n");
    return 0;
}
