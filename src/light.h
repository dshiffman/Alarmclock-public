#pragma once

// Optional BH1750 ambient light sensor on the shared I2C bus (address 0x23, or 0x5C
// with its ADDR pin high). When it's there, the screen goes into night mode when the
// room goes dark and comes back when a light is switched on, and daytime brightness
// follows the room. Without it, night mode follows the clock (night hours in settings).
void light_init();
void light_loop();

bool light_present();
float light_lux();      // smoothed room light, lux (0 if no sensor)
bool light_is_dark();   // room is dark, with hysteresis and a settling delay
