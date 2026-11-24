#pragma once
#include <Arduino.h>

// User settings, saved in flash (NVS) so they survive power cuts.
struct Settings {
  String tz = "PST8PDT,M3.2.0,M11.1.0";  // POSIX TZ string; default US Pacific
  bool use_24h = false;
  bool fahrenheit = true;
  float temp_offset_c = 0.0f;       // added to the sensor reading
  uint8_t day_brightness = 80;      // %
  // Night mode: very dim screen between these times; a tap brightens it briefly.
  bool night_enabled = true;
  uint16_t night_start_min = 22 * 60;  // minutes after midnight (10:00 PM)
  uint16_t night_end_min = 7 * 60;     // 7:00 AM
  uint8_t night_brightness = 2;        // %
  uint8_t tap_wake_secs = 15;          // how long a tap brightens the screen at night
  // Location for outdoor weather and sunrise/sunset. `location` is what the user typed: a town,
  // a ZIP, or "lat, lon". lat/lon/place come from it. Default: Half Moon Bay, CA (change it on
  // the setup or settings page). In hilly places "lat, lon" beats a ZIP, which can land in a
  // valley that's several degrees warmer or cooler.
  String location = "37.4636, -122.4286";
  float lat = 37.4636f, lon = -122.4286f;
  String place = "Half Moon Bay";
  // US National Weather Service stations checked for rain happening now, comma-separated
  // (e.g. "KHAF,KSFO"). Empty = look up the two nearest to lat/lon (US only).
  String wx_stations = "";
  // NOAA tide prediction station ID (tidesandcurrents.noaa.gov), empty = no tides.
  String tide_station = "9414131";  // Pillar Point Harbor, Half Moon Bay
  // Sleep sounds: last choice, remembered for next time.
  uint8_t sleep_sound = 0;       // SleepSound: 0 rain, 1 ocean
  uint8_t sleep_volume = 30;     // %
  uint16_t sleep_minutes = 30;   // 0 = all night
};

extern Settings g_settings;

void settings_load();
void settings_save();

// Limits for the text settings, checked wherever they can be changed.
static constexpr size_t kMaxLocationLen = 63;
static constexpr size_t kMaxTideStationLen = 15;
static constexpr int kMaxWxStations = 4;
// Weather station IDs: up to kMaxWxStations comma-separated IDs of 3-6 letters and digits
// (e.g. "KHAF,LAHC1"), or blank. They go into weather.gov addresses, so nothing else is allowed.
bool settings_valid_wx_stations(const String &list);
// The time zone must be one of kTzOptions.
bool settings_valid_tz(const String &tz);

// Settings are read by the background weather task too; hold this lock while
// changing or copying the String fields.
void settings_lock();
void settings_unlock();

// Common time zones offered during setup: {label, POSIX TZ string}.
struct TzOption {
  const char *label;
  const char *posix;
};
extern const TzOption kTzOptions[];
extern const int kTzOptionCount;
