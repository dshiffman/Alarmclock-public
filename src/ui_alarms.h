#pragma once
#include <lvgl.h>

// Alarm screens: the ringing screen (big Snooze / hold-to-Stop), the alarm list, and the
// alarm editor. They sit on top of the clock screen.

void ui_alarms_init(lv_obj_t *scr);
void ui_alarms_open_list();
void ui_alarms_close_all();
void ui_alarms_update();  // call from ui_update()
bool ui_alarms_open();    // any alarm screen showing (list, editor or ringing)
