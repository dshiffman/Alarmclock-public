#pragma once
#include <lvgl.h>

// Sleep sounds screen: pick rain or ocean, volume and a timer; play or stop. Opened from the
// speaker icon next to the alarm at the top of the clock.

void ui_sleep_init(lv_obj_t *scr);
void ui_sleep_open();
void ui_sleep_close();
bool ui_sleep_is_open();
void ui_sleep_update();  // call from ui_update()
