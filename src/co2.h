#pragma once
#include <stdint.h>

// Optional Sensirion SCD40/SCD41 CO2 sensor on the shared I2C bus (address 0x62).
// The clock works the same without it. With it, the main screen shows "CO2 1,450 ppm ·
// open a window" under the clock when the air gets stuffy (nothing when the air is fine),
// and the settings page and diagnostics show the reading. The SHT31 stays the thermometer:
// the SCD4x's own temperature runs warm from its heater.
// Calibration: the sensor's automatic self-calibration (on by default) assumes the lowest
// level it sees over about a week is fresh outdoor air (~400 ppm). In a room that never gets
// aired out it can drift and read low; airing the room now and then avoids that.
void co2_init();
void co2_loop();

bool co2_present();            // found and has given a reading in the last few minutes
int co2_ppm();                 // latest reading, 0 if none
bool co2_high();               // stuffy: on at 1,400 ppm, off again below 1,200
const char *co2_variant();     // "SCD40", "SCD41", "SCD43", or "none"
uint64_t co2_serial();         // 48-bit serial number, 0 if not found

// Outside air pressure from the weather (hPa). CO2 readings depend on pressure; at
// 500 m (1,600 ft) up they would read about 6 % low without this.
void co2_set_pressure_hpa(float hpa);
