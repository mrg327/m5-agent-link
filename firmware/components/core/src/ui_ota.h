/* The Updating overlay (ui_ota.c): over every screen while a firmware update runs. */
#pragma once
#include "lvgl.h"
#include "ui.h"

void ui_ota_init(lv_obj_t *layer);
/* Opens the overlay (`ver` "" when the daemon gave none) at 0 %. */
void ui_ota_show(const char *ver);
/* waiting: the link is down, the transfer resumes when the computer is back. */
void ui_ota_progress(unsigned pct, bool waiting);
void ui_ota_restarting(void);
void ui_ota_hide(void);
bool ui_ota_visible(void);
void ui_ota_report(ui_report_t *r);
