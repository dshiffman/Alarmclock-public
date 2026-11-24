#include "weather.h"

#include "co2.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

#include "net.h"
#include "settings.h"

static constexpr uint32_t kWeatherEveryMs = 10UL * 60 * 1000;   // 10 min
static constexpr uint32_t kTidesEveryMs = 6UL * 60 * 60 * 1000;  // 6 h
static constexpr uint32_t kRetryMs = 2UL * 60 * 1000;            // after a failure

static SemaphoreHandle_t s_lock = xSemaphoreCreateMutex();
static Outdoor s_outdoor;
static Tides s_tides_raw;  // as fetched; filtered to "upcoming" when read
static volatile bool s_refresh = true;

// ---------- helpers ----------

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
static int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

// "YYYY-MM-DD HH:MM" or "YYYY-MM-DDTHH:MM" -> local minutes since 1970.
static bool parse_local(const char *s, int64_t *out_min, int *hh, int *mm) {
  int y, mo, d, h, mi;
  if (!s || sscanf(s, "%d-%d-%d%*c%d:%d", &y, &mo, &d, &h, &mi) != 5) return false;
  if (out_min) *out_min = days_from_civil(y, mo, d) * 1440 + h * 60 + mi;
  if (hh) *hh = h;
  if (mm) *mm = mi;
  return true;
}

static int64_t now_local_min() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  return days_from_civil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 1440 + t.tm_hour * 60 + t.tm_min;
}

static void format_hm(int h, int m, char *out, size_t n) {
  if (g_settings.use_24h) {
    snprintf(out, n, "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
  }
}

// Weather and tide data isn't sensitive, so the TLS connection skips certificate checks
// rather than carrying a CA bundle that would need updating.
static bool http_get_json(const String &url, JsonDocument &doc, JsonDocument *filter = nullptr) {
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent("AlarmClock/1.0 (ESP32 bedside clock)");  // weather.gov refuses requests without one
  http.useHTTP10(true);  // no chunked encoding, so we can stream-parse
  if (!http.begin(client, url)) return false;
  const int code = http.GET();
  bool ok = false;
  // Every reply we use is under 10 KB. Without certificate checks a spoofed server could send
  // megabytes, and parsing that would eat the memory WiFi needs.
  static constexpr int kMaxReplyBytes = 32 * 1024;
  if (code == HTTP_CODE_OK && http.getSize() > kMaxReplyBytes) {
    Serial.printf("[weather] reply too big (%d bytes) from %s\n", http.getSize(), url.c_str());
  } else if (code == HTTP_CODE_OK) {
    DeserializationError err = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                      : deserializeJson(doc, http.getStream());
    ok = !err;
    if (err) Serial.printf("[weather] JSON error: %s for %s\n", err.c_str(), url.c_str());
  } else {
    char tls[80] = "";
    if (code < 0) client.lastError(tls, sizeof(tls));
    Serial.printf("[weather] HTTP %d %s for %s (heap %u)\n", code, tls, url.c_str(), (unsigned)ESP.getFreeHeap());
  }
  http.end();
  return ok;
}

static String url_encode(const String &s) {
  String out;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

// ---------- fetchers ----------

// Turns the typed town/ZIP into coordinates. Returns true if settings changed.
static bool geocode_if_needed() {
  settings_lock();
  const String query = g_settings.location;
  const bool need = query.length() > 0 && (isnan(g_settings.lat) || g_settings.place.length() == 0);
  settings_unlock();
  if (!need) return false;

  // "lat, lon" (for example 37.4636, -122.4286) is used as is: no lookup.
  float la, lo;
  char extra;
  if (sscanf(query.c_str(), " %f , %f %c", &la, &lo, &extra) == 2 && fabsf(la) <= 90 && fabsf(lo) <= 180) {
    char place[32];
    snprintf(place, sizeof(place), "%.3f, %.3f", la, lo);
    settings_lock();
    g_settings.lat = la;
    g_settings.lon = lo;
    g_settings.place = place;
    settings_unlock();
    settings_save();
    Serial.printf("[weather] location: %s (coordinates)\n", place);
    return true;
  }

  // US ZIP codes are matched as postal codes; anything else is a place name.
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" +
               url_encode(query);
  bool is_zip = query.length() == 5;
  for (char c : query) is_zip &= isdigit((unsigned char)c) != 0;
  if (is_zip) url += "&countryCode=US";

  JsonDocument doc;
  if (!http_get_json(url, doc)) return false;
  JsonObject r = doc["results"][0];
  if (r.isNull()) {
    Serial.printf("[weather] couldn't find \"%s\"\n", query.c_str());
    return false;
  }
  settings_lock();
  g_settings.lat = r["latitude"] | NAN;
  g_settings.lon = r["longitude"] | NAN;
  g_settings.place = String((const char *)(r["name"] | "")) + ", " + String((const char *)(r["admin1"] | ""));
  settings_unlock();
  settings_save();
  Serial.printf("[weather] location: %s (%.3f, %.3f)\n", g_settings.place.c_str(), g_settings.lat, g_settings.lon);
  return true;
}

// ---------- what the sky is doing ----------
// The default model (HRRR/GFS in the US) is good for temperature on a ridge but often an hour
// or two late with rain. So the condition comes from ECMWF (what Windy shows), and if a nearby
// National Weather Service station is actually reporting rain, snow or thunder, that wins.

// ECMWF's current WMO weather code, or -1.
static int fetch_ecmwf_code(float lat, float lon) {
  char url[200];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&models=ecmwf_ifs025&current=weather_code&timezone=auto",
           lat, lon);
  JsonDocument filter;
  filter["current"]["weather_code"] = true;
  JsonDocument doc;
  if (!http_get_json(url, doc, &filter)) return -1;
  return doc["current"]["weather_code"] | -1;
}

// US National Weather Service stations (airports and the like) report what's falling right now.
// Which ones is a setting (wx_stations); blank means "the two nearest", looked up once per
// location. Outside the US the lookup finds nothing and only ECMWF is used.
static constexpr int kStationCount = 2;
static constexpr int kStationMaxAgeMin = 90;  // they report hourly, plus extra reports in bad weather
static constexpr uint32_t kStationLookupRetryMs = 10UL * 60 * 1000;  // after a failed lookup

// Fills in wx_stations with the nearest stations if it's blank. Returns true if it changed.
static bool lookup_stations_if_needed() {
  static uint32_t last_try_ms = 0;
  static float tried_lat = NAN, tried_lon = NAN;  // a new location is looked up straight away
  settings_lock();
  const bool need = g_settings.wx_stations.length() == 0 && !isnan(g_settings.lat);
  const float lat = g_settings.lat, lon = g_settings.lon;
  settings_unlock();
  if (!need) return false;
  if (lat == tried_lat && lon == tried_lon && millis() - last_try_ms < kStationLookupRetryMs) return false;
  tried_lat = lat;
  tried_lon = lon;
  last_try_ms = millis();

  // Step 1: the point's details give the address of its station list (404 outside the US).
  char url[96];
  snprintf(url, sizeof(url), "https://api.weather.gov/points/%.4f,%.4f", lat, lon);
  JsonDocument pfilter;
  pfilter["properties"]["observationStations"] = true;
  JsonDocument point;
  if (!http_get_json(url, point, &pfilter)) return false;
  const String list_url = point["properties"]["observationStations"] | "";
  if (!list_url.startsWith("https://api.weather.gov/")) return false;  // never follow a link elsewhere

  // Step 2: the station list, nearest first. Only ask for a few: the whole list is about 75 KB
  // and arrived cut short on the clock.
  JsonDocument filter;
  filter["features"][0]["properties"]["stationIdentifier"] = true;
  JsonDocument doc;
  if (!http_get_json(list_url + "?limit=" + String(kStationCount), doc, &filter)) return false;
  String ids;
  int n = 0;
  for (JsonObject f : doc["features"].as<JsonArray>()) {
    const char *id = f["properties"]["stationIdentifier"];
    if (!id) continue;
    if (n++) ids += ",";
    ids += id;
    if (n == kStationCount) break;
  }
  if (n == 0 || !settings_valid_wx_stations(ids)) return false;
  // The lookup took a few seconds: only save it if the location is the same and nobody typed
  // stations in meanwhile.
  settings_lock();
  const bool still_wanted = g_settings.wx_stations.length() == 0 && g_settings.lat == lat && g_settings.lon == lon;
  if (still_wanted) g_settings.wx_stations = ids;
  settings_unlock();
  if (!still_wanted) return false;
  settings_save();
  Serial.printf("[weather] nearest weather stations: %s\n", ids.c_str());
  return true;
}

// "Rain", "Light rain", "Showers"... for one NWS present-weather entry, or nullptr if it's
// not precipitation (fog, haze...).
static const char *nws_precip(const char *w, const char *intensity, const char *modifier) {
  if (!w) return nullptr;
  const bool showers = modifier && strcmp(modifier, "showers") == 0;
  const bool light = intensity && strcmp(intensity, "light") == 0;
  const bool heavy = intensity && strcmp(intensity, "heavy") == 0;
  if (strstr(w, "thunder")) return "Thunderstorms";
  if (strstr(w, "freezing") || strstr(w, "ice_pellets")) return "Icy rain";
  if (strstr(w, "hail")) return "Hail";
  if (strstr(w, "snow")) return showers ? "Snow showers" : "Snow";
  if (strstr(w, "drizzle")) return "Drizzle";
  if (strstr(w, "rain")) return showers ? "Showers" : heavy ? "Heavy rain" : light ? "Light rain" : "Rain";
  return nullptr;
}

// If one of the wx_stations is reporting precipitation right now, copies what it is to `out`.
static bool fetch_station_precip(char *out, size_t n) {
  settings_lock();
  const String list = g_settings.wx_stations;
  settings_unlock();
  if (!settings_valid_wx_stations(list)) return false;  // they go into weather.gov addresses

  JsonDocument filter;
  filter["properties"]["timestamp"] = true;
  JsonObject pw = filter["properties"]["presentWeather"].add<JsonObject>();
  pw["weather"] = pw["intensity"] = pw["modifier"] = pw["inVicinity"] = true;

  int start = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    const String id = list.substring(start, comma);
    start = comma + 1;
    if (id.length() == 0) continue;
    JsonDocument doc;
    if (!http_get_json("https://api.weather.gov/stations/" + id + "/observations/latest", doc, &filter)) continue;
    // Skip old reports. Timestamps are UTC, e.g. "2026-11-13T16:55:00+00:00".
    int y, mo, d, h, mi;
    if (sscanf(doc["properties"]["timestamp"] | "", "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) continue;
    const int64_t obs_min = days_from_civil(y, mo, d) * 1440 + h * 60 + mi;
    if ((int64_t)time(nullptr) / 60 - obs_min > kStationMaxAgeMin) continue;

    for (JsonObject e : doc["properties"]["presentWeather"].as<JsonArray>()) {
      if (e["inVicinity"] | false) continue;  // "showers nearby" isn't rain at the station
      const char *what = nws_precip(e["weather"], e["intensity"], e["modifier"]);
      if (what) {
        snprintf(out, n, "%s", what);
        Serial.printf("[weather] %s reports %s\n", id.c_str(), what);
        return true;
      }
    }
  }
  return false;
}

static bool fetch_weather() {
  settings_lock();
  const float lat = g_settings.lat, lon = g_settings.lon;
  settings_unlock();
  if (isnan(lat) || isnan(lon)) return false;

  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code,surface_pressure"
           "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&timezone=auto&forecast_days=5",
           lat, lon);
  JsonDocument doc;
  if (!http_get_json(url, doc)) return false;

  Outdoor o;
  o.temp_c = doc["current"]["temperature_2m"] | NAN;
  o.weather_code = doc["current"]["weather_code"] | -1;
  o.humidity = doc["current"]["relative_humidity_2m"] | NAN;
  co2_set_pressure_hpa(doc["current"]["surface_pressure"] | NAN);  // for the CO2 sensor, if fitted
  o.high_c = doc["daily"]["temperature_2m_max"][0] | NAN;
  o.low_c = doc["daily"]["temperature_2m_min"][0] | NAN;
  int h, m;
  if (parse_local(doc["daily"]["sunrise"][0] | "", nullptr, &h, &m)) o.sunrise_min = h * 60 + m;
  if (parse_local(doc["daily"]["sunset"][0] | "", nullptr, &h, &m)) o.sunset_min = h * 60 + m;
  for (int d = 0; d < Outdoor::kSunDays; d++) {
    if (parse_local(doc["daily"]["sunrise"][d] | "", nullptr, &h, &m)) o.day_sunrise_min[d] = h * 60 + m;
    if (parse_local(doc["daily"]["sunset"][d] | "", nullptr, &h, &m)) o.day_sunset_min[d] = h * 60 + m;
  }
  o.valid = !isnan(o.temp_c);
  o.fetched_ms = millis();
  doc.clear();  // free it before the next requests

  // Condition: a station reporting rain beats ECMWF, which beats the default model.
  const int ecmwf = fetch_ecmwf_code(lat, lon);
  if (!fetch_station_precip(o.condition, sizeof(o.condition)))
    snprintf(o.condition, sizeof(o.condition), "%s", weather_describe(ecmwf >= 0 ? ecmwf : o.weather_code));
  Serial.printf("[weather] %.0f C, %s (default model %d, ECMWF %d)\n", o.temp_c, o.condition, o.weather_code, ecmwf);

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_outdoor = o;
  xSemaphoreGive(s_lock);
  return o.valid;
}

static bool fetch_tides() {
  settings_lock();
  const String station = g_settings.tide_station;
  settings_unlock();
  if (station.length() == 0) return false;

  time_t now = time(nullptr) - 24 * 3600;  // start yesterday, for the curve before the first tide today
  struct tm t;
  localtime_r(&now, &t);
  char begin[12];
  strftime(begin, sizeof(begin), "%Y%m%d", &t);

  String url = "https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?product=predictions"
               "&application=bedroom_alarm_clock&datum=MLLW&time_zone=lst_ldt&units=english"
               "&interval=hilo&format=json&range=168&begin_date=";
  url += begin;
  url += "&station=" + url_encode(station);

  JsonDocument doc;
  if (!http_get_json(url, doc)) return false;
  JsonArray preds = doc["predictions"];
  if (preds.isNull()) {
    Serial.printf("[weather] no tide predictions for station %s\n", station.c_str());
    return false;
  }

  Tides tides;
  const int64_t now_min = now_local_min();
  for (JsonObject p : preds) {
    if (tides.count >= kMaxTides) break;
    TideEvent e;
    int h, m;
    if (!parse_local(p["t"] | "", &e.when, &h, &m)) continue;
    if (e.when < now_min - now_min % 1440 - 14 * 60) continue;  // keep the last tide before today
    e.hm = h * 60 + m;
    e.high = strcmp(p["type"] | "", "H") == 0;
    e.height_ft = atof(p["v"] | "0");
    format_hm(h, m, e.label, sizeof(e.label));
    tides.events[tides.count++] = e;
  }
  tides.valid = tides.count > 0;

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_tides_raw = tides;
  xSemaphoreGive(s_lock);
  return tides.valid;
}

// ---------- background task ----------

static void weather_task(void *) {
  uint32_t next_weather = 0, next_tides = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (net_state() != NetState::Online || !net_time_valid()) continue;

    const uint32_t now = millis();
    if (s_refresh) {
      s_refresh = false;
      next_weather = next_tides = now;
    }
    if (geocode_if_needed()) next_weather = now;
    if (lookup_stations_if_needed()) next_weather = now;

    if ((int32_t)(now - next_weather) >= 0) next_weather = now + (fetch_weather() ? kWeatherEveryMs : kRetryMs);
    if ((int32_t)(now - next_tides) >= 0) next_tides = now + (fetch_tides() ? kTidesEveryMs : kRetryMs);
  }
}

void weather_init() { xTaskCreatePinnedToCore(weather_task, "weather", 12288, nullptr, 1, nullptr, 0); }

void weather_refresh() { s_refresh = true; }

Outdoor weather_outdoor() {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  Outdoor o = s_outdoor;
  xSemaphoreGive(s_lock);
  return o;
}

Tides weather_tides() {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  Tides raw = s_tides_raw;
  xSemaphoreGive(s_lock);
  // Keep the last event before today (to draw the curve from) and everything after it.
  Tides out;
  const int64_t now_min = now_local_min();
  const int64_t today = now_min - now_min % 1440;
  int first = 0;
  for (int i = 0; i < raw.count; i++)
    if (raw.events[i].when <= today) first = i;
  for (int i = first; i < raw.count; i++) out.events[out.count++] = raw.events[i];
  out.valid = out.count >= 2 && out.events[out.count - 1].when > now_min;
  return out;
}

int64_t weather_now_local_min() { return now_local_min(); }

const char *weather_describe(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mostly clear";
    case 2: return "Partly cloudy";
    case 3: return "Cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Icy drizzle";  // short enough for the OUTSIDE card
    case 61: case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Icy rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 99: return "Thunderstorms";
    default: return "";
  }
}
