#pragma once
#include <lvgl.h>

// "Hardware check" screen for first power-up: what's connected, live readings, a touch
// test (a dot follows your finger), a speaker test and backlight steps.
// Open it by holding a finger on the clock screen for 3 seconds.
void ui_diag_init(lv_obj_t *scr);
void ui_diag_open();
void ui_diag_close();
bool ui_diag_is_open();
void ui_diag_update();      // call from ui_update()
int ui_diag_brightness();   // backlight % chosen on the screen, or -1 for normal
