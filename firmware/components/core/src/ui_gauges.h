/* Usage and context gauges on glass (docs/design/GAUGES.md); see ui_gauges.c. Each screen creates its piece
 * once, updates it from its own refresh and adds it to its report. */
#pragma once
#include "lvgl.h"
#include "ui_internal.h"

/* The session card (ui_home_card.c): the context capsule. `shown` is false for anything but the card look,
 * a merged slot or a card that shows a request. */
void ui_gauges_card_create(lv_obj_t *centre);
void ui_gauges_card_update(const session_t *s, bool shown, bool offline);
void ui_gauges_card_report(ui_report_t *r);

/* The empty and ambient faces (ui_home_card.c): "5h 42%" under the caption; `dim` on the ambient face. */
void ui_gauges_face_create(lv_obj_t *centre);
void ui_gauges_face_update(bool shown, bool dim);
void ui_gauges_face_report(ui_report_t *r);

/* STATUS (ui_status.c): two arcs and two rows. Returns whether they are shown: STATUS then sets its clock
 * smaller and higher (ROW1_Y / ROW2_Y below it). */
void ui_gauges_status_create(lv_obj_t *root);
bool ui_gauges_status_render(uint32_t now);
void ui_gauges_status_report(ui_report_t *r);

/* AGENT (ui_agent.c): "Opus 4.7 · Plan · PR 128 approved" under the caption, on the action list only. */
void ui_gauges_agent_create(lv_obj_t *root);
void ui_gauges_agent_update(const session_t *s, bool shown);
void ui_gauges_agent_report(ui_report_t *r);
