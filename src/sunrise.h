#pragma once
// Screen sunrise: before an alarm that has it switched on, the screen slowly goes from black
// through deep red and orange to warm white while the backlight comes up, so the room is
// already light when the alarm rings. Each alarm picks 15, 20 or 30 minutes (or off).

void sunrise_loop();  // call often from loop()

// How far along the sunrise is: 0 (just started, dark) to 1 (full), or -1 when none is running.
float sunrise_progress();

// A quick one to try it: about a minute to rise, then holds full for 10 s and ends.
void sunrise_preview_start();
bool sunrise_previewing();

// A tap on the sunrise turns it off (until that alarm's time has passed). The alarm still rings.
void sunrise_dismiss();

// The alarm the sunrise is leading up to, or -1 (also -1 during a preview).
int sunrise_alarm_index();
