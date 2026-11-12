/* Tabular digits (UI-V3 2.1): every Figtree font draws 0-9 with one advance, so a ticking time, a count or a
 * percentage never moves its neighbours. Measures real strings in every size the UI uses. */
#include <stdio.h>

#include "lvgl.h"
#include "ui_theme.h"

static int fails;

static int width(const lv_font_t *f, const char *s)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

static void same(const char *name, const lv_font_t *f, const char *a, const char *b)
{
    int wa = width(f, a), wb = width(f, b);
    if (wa != wb) {
        printf("FAIL %s: \"%s\" is %d px but \"%s\" is %d px\n", name, a, wa, b, wb);
        fails++;
    }
}

int main(void)
{
    lv_init();
    static const struct {
        const char *name;
        const lv_font_t *f;
    } fonts[] = {{"caption 20/500", FONT_CAPTION}, {"body 20/400", FONT_BODY}, {"strong 20/600", FONT_BODY_STRONG},
                 {"head 24/600", FONT_HEAD}, {"title 34/600", FONT_TITLE}, {"num 56/600", FONT_NUM}};
    for (unsigned i = 0; i < sizeof fonts / sizeof fonts[0]; i++) {
        const lv_font_t *f = fonts[i].f;
        char a[8], b[8];
        for (int d = 1; d <= 9; d++) { /* each digit against 0, alone and inside the shapes the UI prints */
            snprintf(a, sizeof a, "%d", d);
            same(fonts[i].name, f, a, "0");
            snprintf(a, sizeof a, "%d:%d%d", d, d, d);
            snprintf(b, sizeof b, "0:00");
            same(fonts[i].name, f, a, b);
            snprintf(a, sizeof a, "%d%d%%", d, d);
            same(fonts[i].name, f, a, "00%");
        }
        same(fonts[i].name, f, "1:11", "8:88");
        same(fonts[i].name, f, "11:11", "00:00");
        same(fonts[i].name, f, "1m 11s", "0m 00s");
        same(fonts[i].name, f, "Step 1 · 1:11", "Step 0 · 0:00");
        if (width(f, "1111") != 4 * width(f, "0")) {
            printf("FAIL %s: \"1111\" is not four advances of \"0\"\n", fonts[i].name);
            fails++;
        }
    }
    /* proportional text is untouched: the letters still differ in width */
    if (width(FONT_BODY, "iiii") == width(FONT_BODY, "mmmm")) {
        printf("FAIL body font lost its proportional letters\n");
        fails++;
    }
    puts(fails ? "FAILED" : "tabular digits ok");
    return fails ? 1 : 0;
}
