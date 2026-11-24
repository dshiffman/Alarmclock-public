#pragma once

// Screen brightness. Night comes from the room going dark when a BH1750 light sensor
// is fitted, otherwise from the night hours in settings.
//  Day:   normal brightness, full screen.
//  Night: backlight nearly off and only the time shown, in deep amber.
//         A tap brings the full screen back at a gentle brightness for a few seconds.
void nightmode_loop();

bool nightmode_is_night();
