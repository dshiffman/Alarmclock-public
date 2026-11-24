#include "settings.h"

#include <Preferences.h>

Settings g_settings;

static SemaphoreHandle_t s_mutex = xSemaphoreCreateRecursiveMutex();
void settings_lock() { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
void settings_unlock() { xSemaphoreGiveRecursive(s_mutex); }

static const char *kNamespace = "clock";

void settings_load() {
  Preferences p;
  if (!p.begin(kNamespace, true)) return;  // nothing saved yet: keep defaults
  g_settings.tz = p.getString("tz", g_settings.tz);
  g_settings.use_24h = p.getBool("24h", g_settings.use_24h);
  g_settings.fahrenheit = p.getBool("degF", g_settings.fahrenheit);
  g_settings.temp_offset_c = p.getFloat("toff", g_settings.temp_offset_c);
  g_settings.day_brightness = p.getUChar("bright", g_settings.day_brightness);
  g_settings.night_enabled = p.getBool("nightOn", g_settings.night_enabled);
  g_settings.night_start_min = p.getUShort("nightStart", g_settings.night_start_min);
  g_settings.night_end_min = p.getUShort("nightEnd", g_settings.night_end_min);
  g_settings.night_brightness = p.getUChar("nightBright", g_settings.night_brightness);
  g_settings.tap_wake_secs = p.getUChar("tapWake", g_settings.tap_wake_secs);
  g_settings.location = p.getString("loc", g_settings.location);
  g_settings.lat = p.getFloat("lat", g_settings.lat);
  g_settings.lon = p.getFloat("lon", g_settings.lon);
  g_settings.place = p.getString("place", g_settings.place);
  g_settings.tide_station = p.getString("tide", g_settings.tide_station);
  g_settings.wx_stations = p.getString("wxst", g_settings.wx_stations);
  g_settings.sleep_sound = p.getUChar("slpSnd", g_settings.sleep_sound);
  g_settings.sleep_volume = p.getUChar("slpVol", g_settings.sleep_volume);
  g_settings.sleep_minutes = p.getUShort("slpMin", g_settings.sleep_minutes);
  if (g_settings.sleep_sound > 1) g_settings.sleep_sound = 0;  // rain or ocean
  g_settings.sleep_volume = constrain(g_settings.sleep_volume, 5, 100);
  p.end();
}

void settings_save() {
  settings_lock();
  Preferences p;
  if (!p.begin(kNamespace, false)) {
    settings_unlock();
    return;
  }
  p.putString("tz", g_settings.tz);
  p.putBool("24h", g_settings.use_24h);
  p.putBool("degF", g_settings.fahrenheit);
  p.putFloat("toff", g_settings.temp_offset_c);
  p.putUChar("bright", g_settings.day_brightness);
  p.putBool("nightOn", g_settings.night_enabled);
  p.putUShort("nightStart", g_settings.night_start_min);
  p.putUShort("nightEnd", g_settings.night_end_min);
  p.putUChar("nightBright", g_settings.night_brightness);
  p.putUChar("tapWake", g_settings.tap_wake_secs);
  p.putString("loc", g_settings.location);
  p.putFloat("lat", g_settings.lat);
  p.putFloat("lon", g_settings.lon);
  p.putString("place", g_settings.place);
  p.putString("tide", g_settings.tide_station);
  p.putString("wxst", g_settings.wx_stations);
  p.putUChar("slpSnd", g_settings.sleep_sound);
  p.putUChar("slpVol", g_settings.sleep_volume);
  p.putUShort("slpMin", g_settings.sleep_minutes);
  p.end();
  settings_unlock();
}

bool settings_valid_wx_stations(const String &list) {
  int count = 0, len = 0;
  for (size_t i = 0; i <= list.length(); i++) {
    const char c = i < list.length() ? list[i] : ',';
    if (c == ',') {
      if (len == 0 && list.length() > 0) return false;  // empty ID, e.g. "KHAF,,KSFO"
      if (len > 0 && (len < 3 || ++count > kMaxWxStations)) return false;
      len = 0;
    } else if (isalnum((unsigned char)c) && len < 6) {
      len++;
    } else {
      return false;
    }
  }
  return true;
}

bool settings_valid_tz(const String &tz) {
  for (int i = 0; i < kTzOptionCount; i++)
    if (tz == kTzOptions[i].posix) return true;
  return false;
}

const TzOption kTzOptions[] = {
    {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Arizona", "MST7"},
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"US Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"US Hawaii", "HST10"},
    {"UK / Ireland", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Eastern Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"India", "IST-5:30"},
    {"Japan", "JST-9"},
    {"Australia East", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"UTC", "UTC0"},
};
const int kTzOptionCount = sizeof(kTzOptions) / sizeof(kTzOptions[0]);
