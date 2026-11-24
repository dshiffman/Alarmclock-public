#pragma once
#include <stdint.h>

// Starts the RGB panel, the GT911 touch controller and LVGL.
// Also starts the shared I2C bus (Wire) that the sensor uses.
bool display_init();

// Screen brightness, 0-100 %. 0 turns the backlight fully off.
void display_set_brightness(uint8_t percent);
uint8_t display_get_brightness();

// Milliseconds since the screen was last touched.
uint32_t display_ms_since_touch();
// Count a press of the physical button as a touch (wakes the screen at night).
void display_poke();

// While "dimmed" is set, a tap only wakes the screen: the touch is not passed on to
// the UI, so a sleepy tap at night can't press a button by accident.
void display_set_dimmed(bool dimmed);

// Screens' worth of pixels redrawn per minute, averaged since the last call (diagnostics).
float display_draw_rate();

// Print each redrawn area on serial for the next `ms` (serial command 'f').
void display_log_flushes(uint32_t ms);

// Print what the touch panel reports, 5 times a second, for the next `ms` (serial command 't').
void display_log_touch(uint32_t ms);
