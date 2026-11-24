#pragma once
#include <stdint.h>

// Indoor temperature/humidity. Uses a Sensirion SHT4x (SHT40/41/45) or SHT3x (SHT30/31/35) on the shared I2C bus
// if one answers, otherwise an AM2302/DHT22 on PIN_AM2302.

struct IndoorReading {
  bool valid = false;
  float temp_c = 0;
  float humidity = 0;  // % relative humidity
};

// Number of points in the history buffer: one every 10 minutes for 24 hours.
static constexpr int kHistoryPoints = 144;
static constexpr uint32_t kHistoryIntervalMs = 10UL * 60UL * 1000UL;

// Call after display_init() (which starts the I2C bus).
void sensor_init();

// Call from loop(); reads the sensor every few seconds.
void sensor_loop();

bool sensor_present();
const char *sensor_name();  // "SHT4x", "SHT3x", "AM2302" or "none"
IndoorReading sensor_latest();

// Fills `out` (oldest first) with up to kHistoryPoints temperature (C) or humidity (%) samples.
// Returns how many points were written.
int sensor_history_temp(float *out, int max_points);
int sensor_history_humidity(float *out, int max_points);

// Temperature offset in C added to every reading, to correct for heat from the board.
void sensor_set_temp_offset(float offset_c);
