#include "web.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include <algorithm>

#include "alarm.h"
#include "audio.h"
#include "co2.h"
#include "display.h"
#include "light.h"
#include "nightmode.h"
#include "net.h"
#include "sensor.h"
#include "settings.h"
#include "sleep.h"
#include "sunrise.h"
#include "ui_alarms.h"
#include "weather.h"
#include "web_files.h"  // generated from web/ by tools/embed_web.py

static constexpr uint32_t kPreviewMs = 20000;  // one whole preview fade-up (18 to 19.2 s)
// Bodies are small JSON and must arrive whole within a couple of seconds. (The server has its
// own task, so a slow client can't hold up the clock; this just keeps it from tying up the
// server, and its memory, for long.)
static constexpr size_t kMaxBodyBytes = 2048;
static constexpr uint32_t kMaxBodyMs = 2000;

static WebServer s_server(80);
static bool s_started = false;
static uint32_t s_preview_until = 0;

// ---------- the lock shared with the main loop ----------

static SemaphoreHandle_t s_main_lock = xSemaphoreCreateRecursiveMutex();
void main_lock() { xSemaphoreTakeRecursive(s_main_lock, portMAX_DELAY); }
void main_unlock() { xSemaphoreGiveRecursive(s_main_lock); }

// ---------- replies ----------
// Handlers run with the main lock held, so they only write their reply here; it's sent after
// the lock is released (sending can wait seconds on a slow phone).

static int s_reply_code = 0;
static const char *s_reply_type = "text/plain";
static String s_reply_body;

static void send_error(int code, const char *msg) {
  s_reply_code = code;
  s_reply_type = "text/plain";
  s_reply_body = msg;
}

static void flush_reply() {
  if (!s_reply_code) return;
  if (s_reply_code == 200) s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(s_reply_code, s_reply_type, s_reply_body);
  s_reply_code = 0;
  s_reply_body = String();  // free it
}

// ---------- who may use the interface ----------
// Any web page a phone on the home WiFi opens could otherwise send requests here (setting a
// 3 AM alarm, deleting alarms) or, by pointing its own domain name at the clock (DNS
// rebinding), read /api/state. So:
// - the Host must be the clock's own name or address (a rebinding page sends its own name),
// - changes must be application/json (other sites can't send that without asking first, and
//   the clock never says yes), and
// - if the browser says which page sent it (Origin), that must be the clock's own page.

static String host_name() {  // Host header without the port, lower case
  String h = s_server.hostHeader();
  h.toLowerCase();
  const int colon = h.indexOf(':');
  return colon < 0 ? h : h.substring(0, colon);
}

static bool host_ok() {
  const String h = host_name();
  return h == String(kHostname) + ".local" || h == kHostname || h == WiFi.localIP().toString();
}

static bool request_ok(bool is_change) {
  if (!host_ok()) {
    send_error(403, "Use http://alarmclock.local or the clock's own address");
    return false;
  }
  if (!is_change) return true;
  if (!s_server.header("Content-Type").startsWith("application/json")) {
    send_error(415, "Expected JSON");
    return false;
  }
  const String origin = s_server.header("Origin");
  if (origin.length() && origin != "http://" + s_server.hostHeader()) {
    send_error(403, "Not from the clock's own page");
    return false;
  }
  return true;
}

// A POST endpoint taking a small JSON body. It reads the body itself instead of letting the
// server do it, so it can refuse anything over kMaxBodyBytes or slower than kMaxBodyMs (the
// server would otherwise wait as long as bytes keep trickling in, freezing the clock).
class JsonPost : public RequestHandler {
 public:
  JsonPost(const char *uri, void (*fn)(JsonDocument &)) : _uri(uri), _fn(fn) {}

  bool canHandle(WebServer &, HTTPMethod method, const String &uri) override {
    return method == HTTP_POST && uri == _uri;
  }
  bool canRaw(WebServer &, const String &uri) override { return uri == _uri; }

  void raw(WebServer &server, const String &, HTTPRaw &r) override {
    if (r.status == RAW_START) {
      _body = "";
      _start_ms = millis();
      _refused = server.clientContentLength() > (int)kMaxBodyBytes;
      // Each read waits at most this long, so the time limit below is checked often (the
      // server's own 5 s wait is restored for sending the reply).
      server.client().setTimeout(250);
    } else if (r.status == RAW_END) {
      server.client().setTimeout(HTTP_MAX_SEND_WAIT);
    } else if (r.status == RAW_WRITE && !_refused) {
      if (_body.length() + r.currentSize > kMaxBodyBytes || millis() - _start_ms > kMaxBodyMs)
        _refused = true;
      else
        _body.concat((const char *)r.buf, r.currentSize);
    }
    // Refused: drop the connection, which ends the server's read loop straight away.
    if (_refused) server.client().stop();
  }

  bool handle(WebServer &, HTTPMethod, const String &) override {
    if (_refused) return true;  // connection already dropped
    JsonDocument body;
    if (!request_ok(true)) {
      // reply already set
    } else if (deserializeJson(body, _body)) {
      send_error(400, "Bad JSON");
    } else {
      main_lock();
      _fn(body);
      main_unlock();
    }
    _body = String();  // free it
    flush_reply();
    return true;
  }

 private:
  String _uri;
  void (*_fn)(JsonDocument &);
  String _body;
  uint32_t _start_ms = 0;
  bool _refused = false;
};

static void add_state(JsonDocument &d) {
  if (net_time_valid()) {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char b[40];
    alarm_format_time(t.tm_hour, t.tm_min, b, sizeof(b));
    d["time"] = b;
    strftime(b, sizeof(b), "%A, %B %e", &t);
    char *dbl = strstr(b, "  ");  // %e pads single-digit days
    if (dbl) memmove(dbl, dbl + 1, strlen(dbl));
    d["date"] = b;
  }

  const IndoorReading r = sensor_latest();
  JsonObject in = d["inside"].to<JsonObject>();
  in["ok"] = r.valid;
  in["t"] = r.temp_c;
  in["h"] = r.humidity;
  if (co2_present()) {  // optional CO2 sensor
    in["co2"] = co2_ppm();
    in["co2High"] = co2_high();  // the clock's own call (on at 1,400 ppm, off below 1,200)
  }

  const Outdoor o = weather_outdoor();
  JsonObject out = d["outside"].to<JsonObject>();
  out["ok"] = o.valid;
  out["t"] = o.temp_c;
  out["hi"] = o.high_c;
  out["lo"] = o.low_c;
  if (isnan(o.humidity)) out["h"] = nullptr; else out["h"] = o.humidity;
  out["cond"] = String(o.condition);  // copied: `o` is gone by the time the reply is written

  settings_lock();
  const Settings &g = g_settings;
  JsonObject s = d["s"].to<JsonObject>();
  s["h24"] = g.use_24h;
  s["degF"] = g.fahrenheit;
  s["bright"] = g.day_brightness;
  s["nightOn"] = g.night_enabled;
  s["nightStart"] = g.night_start_min;
  s["nightEnd"] = g.night_end_min;
  s["nightBright"] = g.night_brightness;
  s["tapWake"] = g.tap_wake_secs;
  s["toff"] = g.temp_offset_c;
  s["tz"] = g.tz;
  s["loc"] = g.location;
  s["wxst"] = g.wx_stations;
  s["tide"] = g.tide_station;
  d["place"] = g.place;
  settings_unlock();

  JsonArray tzs = d["tzs"].to<JsonArray>();
  for (int i = 0; i < kTzOptionCount; i++) {
    JsonArray z = tzs.add<JsonArray>();
    z.add(kTzOptions[i].label);
    z.add(kTzOptions[i].posix);
  }
  JsonArray sounds = d["sounds"].to<JsonArray>();
  for (int i = 0; i < (int)AlarmSound::Count; i++) sounds.add(audio_sound_name((AlarmSound)i));

  // Alarms in time order, as on the clock's own list.
  int order[kMaxAlarms], n = 0;
  for (int i = 0; i < kMaxAlarms; i++)
    if (alarm_get(i).used) order[n++] = i;
  std::sort(order, order + n, [](int a, int b) {
    return alarm_get(a).hour * 60 + alarm_get(a).minute < alarm_get(b).hour * 60 + alarm_get(b).minute;
  });
  JsonArray alarms = d["alarms"].to<JsonArray>();
  for (int k = 0; k < n; k++) {
    const Alarm &a = alarm_get(order[k]);
    JsonObject j = alarms.add<JsonObject>();
    j["i"] = order[k];
    j["on"] = a.enabled;
    j["h"] = a.hour;
    j["m"] = a.minute;
    j["days"] = a.days;
    j["snd"] = (int)a.sound;
    j["vol"] = a.volume;
    j["snz"] = a.snooze_min;
    j["ramp"] = a.ramp;
    j["skip"] = a.skip_next;
    j["sun"] = a.sunrise_min;
  }
  JsonArray ss = d["sleepSounds"].to<JsonArray>();
  for (int i = 0; i < (int)SleepSound::Count; i++) ss.add(sleep_sound_name((SleepSound)i));
  JsonArray st = d["sleepTimers"].to<JsonArray>();
  for (uint16_t m : kSleepTimerChoices) {
    JsonArray c = st.add<JsonArray>();
    c.add(sleep_timer_label(m));
    c.add(m);
  }
  JsonObject sl = d["sleep"].to<JsonObject>();
  sl["on"] = sleep_active();
  sl["snd"] = g_settings.sleep_sound;
  sl["vol"] = g_settings.sleep_volume;
  sl["min"] = g_settings.sleep_minutes;
  sl["left"] = sleep_minutes_left();
  d["lightSensor"] = light_present();  // with one, night mode follows the room, not the hours
  d["maxAlarms"] = kMaxAlarms;
  d["alarmsRev"] = alarm_revision();

  const uint32_t up_min = millis() / 60000;
  char up[24];
  if (up_min < 60) snprintf(up, sizeof(up), "%lu min", (unsigned long)up_min);
  else snprintf(up, sizeof(up), "%lu h %lu min", (unsigned long)(up_min / 60), (unsigned long)(up_min % 60));
  d["up"] = up;
  d["ip"] = WiFi.localIP().toString();
  d["wifi"] = WiFi.SSID();
  d["heap"] = ESP.getFreeHeap() / 1024;
  d["heapMin"] = ESP.getMinFreeHeap() / 1024;  // lowest since boot (weather downloads dip it)
  d["drawRate"] = roundf(display_draw_rate() * 10) / 10;  // screens redrawn per minute, since last ask
  d["lux"] = roundf(light_lux() * 10) / 10;               // room light (0 without a sensor)
  d["backlight"] = display_get_brightness();              // current backlight %
  d["night"] = nightmode_is_night();
}

// Called with the main lock held: writes the reply for flush_reply() to send.
static void send_state() {
  JsonDocument d;
  add_state(d);
  s_reply_code = 200;
  s_reply_type = "application/json";
  s_reply_body = String();
  serializeJson(d, s_reply_body);
}

static void handle_get_state() {
  if (request_ok(false)) {
    main_lock();
    send_state();
    main_unlock();
  }
  flush_reply();
}

// The clock's own alarm screens show what they had when opened; close them so they don't
// show (and save) stale alarms. Never while an alarm is ringing.
static void alarms_changed() {
  if (ui_alarms_open() && alarm_state() == AlarmState::Idle) ui_alarms_close_all();
}

// ---------- handlers ----------

// Text settings are checked before anything is changed, so a bad request changes nothing.
static bool text_setting(JsonDocument &b, const char *key, String &out, size_t max_len) {
  if (!b[key].is<const char *>()) return false;
  out = b[key].as<const char *>();
  out.trim();
  return out.length() <= max_len;
}

static void handle_settings(JsonDocument &b) {
  bool tz_changed = false, weather_changed = false;
  String loc, wxst, tide;
  const bool has_loc = b["loc"].is<const char *>(), has_wxst = b["wxst"].is<const char *>(),
             has_tide = b["tide"].is<const char *>();
  if (has_loc && !text_setting(b, "loc", loc, kMaxLocationLen))
    return send_error(400, "Location is too long");
  if (has_tide && !text_setting(b, "tide", tide, kMaxTideStationLen))
    return send_error(400, "Tide station ID is too long");
  if (has_wxst) {
    wxst = b["wxst"].as<const char *>();
    wxst.toUpperCase();
    wxst.replace(" ", "");
    if (!settings_valid_wx_stations(wxst))
      return send_error(400, "Weather stations: up to 4 IDs like KHAF, separated by commas");
  }
  if (b["tz"].is<const char *>() && !settings_valid_tz(b["tz"].as<const char *>()))
    return send_error(400, "Unknown time zone");

  settings_lock();
  Settings &s = g_settings;
  if (b["h24"].is<bool>()) s.use_24h = b["h24"];
  if (b["degF"].is<bool>()) s.fahrenheit = b["degF"];
  if (b["bright"].is<int>()) s.day_brightness = constrain(b["bright"].as<int>(), 5, 100);
  if (b["nightOn"].is<bool>()) s.night_enabled = b["nightOn"];
  if (b["nightStart"].is<int>()) s.night_start_min = constrain(b["nightStart"].as<int>(), 0, 1439);
  if (b["nightEnd"].is<int>()) s.night_end_min = constrain(b["nightEnd"].as<int>(), 0, 1439);
  if (b["nightBright"].is<int>()) s.night_brightness = constrain(b["nightBright"].as<int>(), 1, 30);
  if (b["tapWake"].is<int>()) s.tap_wake_secs = constrain(b["tapWake"].as<int>(), 1, 120);
  if (b["toff"].is<float>()) s.temp_offset_c = constrain(b["toff"].as<float>(), -10.0f, 10.0f);
  if (b["tz"].is<const char *>()) {
    s.tz = b["tz"].as<const char *>();
    tz_changed = true;
  }
  if (has_loc && loc != s.location) {
    s.location = loc;
    s.lat = s.lon = NAN;  // looked up again by the weather task
    s.place = "";
    if (!has_wxst) s.wx_stations = "";  // and the nearest stations to the new place
    weather_changed = true;
  }
  if (has_wxst) {
    s.wx_stations = wxst;
    weather_changed = true;
  }
  if (has_tide) {
    s.tide_station = tide;
    weather_changed = true;
  }
  const float toff = s.temp_offset_c;
  settings_unlock();

  settings_save();
  sensor_set_temp_offset(toff);
  if (tz_changed) net_apply_timezone();
  if (weather_changed) weather_refresh();
  String keys;  // log what changed, not the whole request
  for (JsonPair kv : b.as<JsonObject>()) keys += String(keys.length() ? ", " : "") + kv.key().c_str();
  Serial.printf("[web] settings saved: %s\n", keys.c_str());
  send_state();
}

// Alarm edits carry the alarm revision the page last saw. If anything changed since (on the
// clock's screen, or by itself), the edit is refused rather than overwriting the newer alarm.
static bool alarms_current(JsonDocument &b) {
  if (b["rev"].is<uint32_t>() && b["rev"].as<uint32_t>() == alarm_revision()) return true;
  send_error(409, "The alarms changed on the clock. Showing the latest; please try again.");
  return false;
}

static void handle_alarm(JsonDocument &b) {
  if (!alarms_current(b)) return;
  int i = b["i"] | -1;
  if (i < 0) {
    i = alarm_add();
    if (i < 0) return send_error(409, "All 10 alarms are in use");
  } else if (i >= kMaxAlarms || !alarm_get(i).used) {
    return send_error(404, "No such alarm");
  }
  Alarm a = alarm_get(i);
  if (b["on"].is<bool>()) a.enabled = b["on"];
  if (b["h"].is<int>()) a.hour = constrain(b["h"].as<int>(), 0, 23);
  if (b["m"].is<int>()) a.minute = constrain(b["m"].as<int>(), 0, 59);
  if (b["days"].is<int>()) a.days = b["days"].as<int>() & 0x7F;
  if (b["snd"].is<int>()) a.sound = (AlarmSound)constrain(b["snd"].as<int>(), 0, (int)AlarmSound::Count - 1);
  if (b["vol"].is<int>()) a.volume = constrain(b["vol"].as<int>(), 10, 100);
  if (b["snz"].is<int>()) a.snooze_min = constrain(b["snz"].as<int>(), 1, 60);
  if (b["ramp"].is<bool>()) a.ramp = b["ramp"];
  if (b["skip"].is<bool>()) a.skip_next = b["skip"];
  if (b["sun"].is<int>())
    for (uint8_t c : kSunriseChoices)
      if (c == b["sun"].as<int>()) a.sunrise_min = c;
  alarm_set(i, a);
  alarms_changed();
  Serial.printf("[web] alarm %d saved: %02d:%02d %s\n", i, a.hour, a.minute, a.enabled ? "on" : "off");
  send_state();
}

static void handle_alarm_delete(JsonDocument &b) {
  if (!alarms_current(b)) return;
  const int i = b["i"] | -1;
  if (i < 0 || i >= kMaxAlarms || !alarm_get(i).used) return send_error(404, "No such alarm");
  alarm_remove(i);
  alarms_changed();
  Serial.printf("[web] alarm %d deleted\n", i);
  send_state();
}

static void handle_preview(JsonDocument &b) {
  if (alarm_state() == AlarmState::Ringing) return send_error(409, "An alarm is ringing");
  if (b["stop"] | false) {
    audio_stop();
    s_preview_until = 0;
  } else {
    const int snd = constrain(b["snd"] | 0, 0, (int)AlarmSound::Count - 1);
    audio_play((AlarmSound)snd, constrain(b["vol"] | 70, 10, 100), b["ramp"] | false, true);
    // {secs}: a longer listen, e.g. to hear how a song clip ends.
    s_preview_until = millis() + (b["secs"].is<int>() ? constrain(b["secs"].as<int>(), 5, 120) * 1000UL : kPreviewMs);
  }
  send_state();
}

// Runs the sped-up sunrise on the clock (about a minute), or {stop: true} ends it.
static void handle_sunrise_preview(JsonDocument &b) {
  if (alarm_state() == AlarmState::Ringing) return send_error(409, "An alarm is ringing");
  if (b["stop"] | false) {
    if (sunrise_previewing()) sunrise_dismiss();
  } else {
    sunrise_preview_start();
  }
  send_state();
}

// {snd, vol, min, play}: any of them. Picking a sound with play: true starts it.
static void handle_sleep(JsonDocument &b) {
  if (b["snd"].is<int>()) sleep_set_sound((SleepSound)constrain(b["snd"].as<int>(), 0, (int)SleepSound::Count - 1));
  if (b["vol"].is<int>()) sleep_set_volume(constrain(b["vol"].as<int>(), 5, 100));
  if (b["min"].is<int>()) {
    const int m = b["min"].as<int>();
    for (uint16_t c : kSleepTimerChoices)
      if (c == m) sleep_set_timer(c);
  }
  if (b["play"].is<bool>()) {
    if (!b["play"].as<bool>())
      sleep_stop();
    else if (alarm_state() == AlarmState::Ringing)
      return send_error(409, "An alarm is ringing");
    else if (!sleep_active())
      sleep_start();
  }
  send_state();
}

static void serve_file(const WebFile &f) {
  s_server.sendHeader("Content-Encoding", "gzip");
  s_server.sendHeader("Cache-Control", "no-cache");  // always check for a newer page after a firmware update
  s_server.send_P(200, f.type, (const char *)f.data, f.len);
}

static void start() {
  for (const WebFile &f : kWebFiles) s_server.on(f.path, HTTP_GET, [&f]() { serve_file(f); });
  s_server.on("/api/state", HTTP_GET, handle_get_state);
  s_server.addHandler(new JsonPost("/api/settings", handle_settings));
  s_server.addHandler(new JsonPost("/api/alarm", handle_alarm));
  s_server.addHandler(new JsonPost("/api/alarm/delete", handle_alarm_delete));
  s_server.addHandler(new JsonPost("/api/preview", handle_preview));
  s_server.addHandler(new JsonPost("/api/sleep", handle_sleep));
  s_server.addHandler(new JsonPost("/api/sunrise/preview", handle_sunrise_preview));
  s_server.onNotFound([]() {
    send_error(404, "Not found");
    flush_reply();
  });
  static const char *kHeaders[] = {"Content-Type", "Origin"};  // read by request_ok()
  s_server.collectHeaders(kHeaders, 2);
  s_server.begin();
  s_started = true;
  Serial.printf("[web] settings page at http://%s.local (http://%s)\n", kHostname, WiFi.localIP().toString().c_str());
}

static void web_task(void *) {
  for (;;) {
    if (!s_started) {
      // Wait until online: during first-time setup, the WiFi setup page uses port 80.
      if (net_state() == NetState::Online) start();
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    s_server.handleClient();
    if (s_preview_until && (int32_t)(millis() - s_preview_until) >= 0) {
      s_preview_until = 0;
      main_lock();
      if (alarm_state() != AlarmState::Ringing) audio_stop();
      main_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// Same core and priority as the main loop, which releases the lock during its pause each cycle.
void web_init() { xTaskCreatePinnedToCore(web_task, "web", 8192, nullptr, 1, nullptr, 1); }
