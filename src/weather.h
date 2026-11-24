#pragma once
#include <Arduino.h>

// Outdoor weather, sunrise/sunset (Open-Meteo, no API key) and tide predictions
// (NOAA CO-OPS, US stations, no API key). Fetched on a background task so the
// screen never stalls on the network.

struct Outdoor {
  bool valid = false;
  float temp_c = 0;
  float high_c = 0, low_c = 0;
  int weather_code = -1;          // WMO code from the default model
  char condition[20] = "";        // what to show, e.g. "Rain" (see fetch_weather for where it comes from)
  float humidity = NAN;           // % relative humidity
  int sunrise_min = -1;           // minutes after local midnight
  int sunset_min = -1;
  // Today and the next days (for the daylight shading on the tide chart); -1 if unknown.
  static constexpr int kSunDays = 5;
  int day_sunrise_min[kSunDays] = {-1, -1, -1, -1, -1};
  int day_sunset_min[kSunDays] = {-1, -1, -1, -1, -1};
  uint32_t fetched_ms = 0;
};

struct TideEvent {
  bool high = false;
  int64_t when = 0;   // local "minutes since 1970" (YYYY-MM-DD HH:MM as given by NOAA)
  int hm = 0;           // minutes after local midnight
  char label[20] = "";  // e.g. "3:12 PM" or "15:12"
  float height_ft = 0;
};

static constexpr int kMaxTides = 32;  // ~4 a day; covers today plus 5 more days

struct Tides {
  bool valid = false;
  int count = 0;
  TideEvent events[kMaxTides];  // in time order, starting with the last one before today
};

void weather_init();

// Copies of the latest data (safe to call from the UI loop).
Outdoor weather_outdoor();
Tides weather_tides();

// Local time as minutes since 1970, same scale as TideEvent::when.
int64_t weather_now_local_min();

// Ask the background task to refresh now (e.g. after the location changes).
void weather_refresh();

// Short description for a WMO weather code, e.g. "Cloudy".
const char *weather_describe(int code);
