#pragma once

// Builds the main clock screen.
void ui_init();

// Night look: only the time, in a deep amber, everything else hidden.
void ui_set_night(bool night);

// Refreshes time, readings and status. Cheap to call often; only redraws what changed.
void ui_update();
