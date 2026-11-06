#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hal.h"
#include "lvgl.h"

#if AGENTLINK_HAVE_CORE
#include "core.h"
#endif

#if !AGENTLINK_HAVE_CORE
static const char *TAG = "main";

static void hello_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "agent-link\nhello");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}
#endif

void app_main(void)
{
    hal_board_init();

#if AGENTLINK_HAVE_CORE
    core_init(); /* lv_init, display, touch, link, UI */
    for (;;) {
        uint32_t wait_ms = core_step();
        vTaskDelay(pdMS_TO_TICKS(wait_ms < 1 ? 1 : wait_ms));
    }
#else
    lv_init();
    lv_display_t *disp = hal_display_create();
    if (disp == NULL) {
        ESP_LOGE(TAG, "display init failed");
    }
    hal_touch_create(disp);
    hello_screen();
    hal_debug_line("{\"cat\":\"ui\",\"ev\":\"hello\"}");
    for (;;) {
        uint32_t wait_ms = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(wait_ms < 5 ? 5 : (wait_ms > 33 ? 33 : wait_ms)));
    }
#endif
}
