#pragma once
#include <lvgl.h>

// The screen sunrise: a full-screen glow that warms from black to warm white (see sunrise.h).
// A tap turns it off.

void ui_sunrise_init(lv_obj_t *scr);
void ui_sunrise_update();  // call often (the colour moves smoothly)
bool ui_sunrise_is_open();
