/* Shared by the ui*.c files only. */
#pragma once
#include <stdarg.h>

#include "model.h"
#include "ui.h"

#define UI_SIZE 466
#define UI_CX 233
#define UI_CY 233
#define UI_R 233
#define UI_HOLD_MS 1200 /* hold-to-confirm duration (DESIGN §6) */

typedef enum { SCR_GLANCE, SCR_LIST, SCR_DETAIL } screen_id_t;
typedef enum { NAV_FORWARD, NAV_BACK, NAV_UP, NAV_DOWN } ui_nav_t;

typedef struct {
    lv_obj_t *root;
    void (*on_show)(void);
    void (*refresh)(void);                    /* model or link changed */
    void (*tick)(uint32_t now_ms);
    bool (*gesture)(const gesture_evt_t *g);
    void (*report)(ui_report_t *r);
} ui_screen_t;

extern const ui_actions_t *g_ui_actions;
extern ui_screen_t g_glance, g_list, g_detail;
void ui_glance_init(void);
void ui_list_init(void);
void ui_detail_init(void);

bool ui_list_tap_navigates(int y); /* a tap here would leave the list */
void ui_goto(screen_id_t id, ui_nav_t nav);
screen_id_t ui_current(void);
bool ui_link_connected(void);
int ui_link_transport(void); /* -1 none, else hal_link_kind_t */
bool ui_animations_allowed(void);
void ui_select(uint32_t sid);
bool ui_selected(uint32_t *sid);
void ui_report_add(ui_report_t *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
bool ui_hit(const lv_obj_t *obj, int x, int y, int slop);
lv_obj_t *ui_new_screen(void);
