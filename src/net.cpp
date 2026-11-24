#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <time.h>

#include "display.h"
#include "settings.h"
#include "weather.h"

static WiFiManager s_wm;
static NetState s_state = NetState::Connecting;
static bool s_mdns_started = false;
static bool s_time_synced = false;

// Setup-page fields. The time zone is a drop-down that writes into a hidden field.
static String s_tz_html;
static WiFiManagerParameter *s_p_tz_select = nullptr;
static WiFiManagerParameter *s_p_tz = nullptr;
static WiFiManagerParameter *s_p_24h = nullptr;
static WiFiManagerParameter *s_p_degf = nullptr;
static WiFiManagerParameter *s_p_loc = nullptr;
static WiFiManagerParameter *s_p_tide = nullptr;
static WiFiManagerParameter *s_p_wxst = nullptr;

static void on_time_sync(struct timeval *) { s_time_synced = true; }

static void build_setup_fields() {
  s_tz_html =
      "<br><label for='tzsel'>Time zone</label>"
      "<select id='tzsel' onchange=\"document.getElementById('tz').value=this.value\">";
  for (int i = 0; i < kTzOptionCount; i++) {
    s_tz_html += "<option value='";
    s_tz_html += kTzOptions[i].posix;
    s_tz_html += "'";
    if (g_settings.tz == kTzOptions[i].posix) s_tz_html += " selected";
    s_tz_html += ">";
    s_tz_html += kTzOptions[i].label;
    s_tz_html += "</option>";
  }
  s_tz_html += "</select>";

  s_p_tz_select = new WiFiManagerParameter(s_tz_html.c_str());
  s_p_tz = new WiFiManagerParameter("tz", "", g_settings.tz.c_str(), 63, "type='hidden'");
  s_p_24h = new WiFiManagerParameter("h24", "24-hour clock", "1", 2,
                                     g_settings.use_24h ? "type='checkbox' checked" : "type='checkbox'",
                                     WFM_LABEL_AFTER);
  s_p_degf = new WiFiManagerParameter("degf", "Show temperature in &deg;F", "1", 2,
                                      g_settings.fahrenheit ? "type='checkbox' checked" : "type='checkbox'",
                                      WFM_LABEL_AFTER);
  s_p_loc = new WiFiManagerParameter("loc", "Town, ZIP, or lat, lon (for weather and sunrise)", g_settings.location.c_str(), 63);
  s_p_tide = new WiFiManagerParameter("tide", "NOAA tide station ID (optional)", g_settings.tide_station.c_str(), 15);
  s_wm.addParameter(s_p_tz_select);
  s_wm.addParameter(s_p_tz);
  s_wm.addParameter(s_p_24h);
  s_wm.addParameter(s_p_degf);
  s_wm.addParameter(s_p_loc);
  s_wm.addParameter(s_p_tide);
  s_p_wxst = new WiFiManagerParameter("wxst", "Weather stations for rain now (US, optional; blank = nearest)",
                                      g_settings.wx_stations.c_str(), 31);
  s_wm.addParameter(s_p_wxst);
}

static void on_params_saved() {
  settings_lock();
  if (settings_valid_tz(s_p_tz->getValue())) g_settings.tz = s_p_tz->getValue();
  String loc = s_p_loc->getValue();
  loc.trim();
  String wxst = s_p_wxst->getValue();
  wxst.trim();
  wxst.toUpperCase();
  wxst.replace(" ", "");
  if (loc != g_settings.location) {
    g_settings.location = loc;
    g_settings.lat = g_settings.lon = NAN;  // look the new place up
    g_settings.place = "";
    // New place: find its nearest weather stations, unless different ones were typed in too.
    if (wxst == g_settings.wx_stations) wxst = "";
  }
  if (settings_valid_wx_stations(wxst)) g_settings.wx_stations = wxst;  // otherwise keep the old ones
  String tide = s_p_tide->getValue();
  tide.trim();
  if (tide.length() <= kMaxTideStationLen) g_settings.tide_station = tide;
  g_settings.use_24h = strcmp(s_p_24h->getValue(), "1") == 0;
  g_settings.fahrenheit = strcmp(s_p_degf->getValue(), "1") == 0;
  settings_unlock();
  settings_save();
  net_apply_timezone();
  weather_refresh();
  Serial.printf("[net] settings saved: tz=%s 24h=%d degF=%d\n", g_settings.tz.c_str(), g_settings.use_24h,
                g_settings.fahrenheit);
}

void net_apply_timezone() {
  setenv("TZ", g_settings.tz.c_str(), 1);
  tzset();
}

void net_init() {
  net_apply_timezone();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  build_setup_fields();
  s_wm.setTitle("Alarm Clock Setup");
  s_wm.setHostname(kHostname);
  s_wm.setConfigPortalBlocking(false);
  s_wm.setConnectTimeout(20);
  s_wm.setSaveParamsCallback(on_params_saved);
  static std::vector<const char *> menu = {"wifi", "sep", "restart"};
  s_wm.setMenu(menu);
  s_wm.setClass("invert");  // dark page, easier on the eyes at night

  if (s_wm.autoConnect(kSetupApName)) {
    s_state = NetState::Online;
  } else {
    s_state = NetState::SetupPortal;
    Serial.printf("[net] no WiFi; join \"%s\" from your phone to set up\n", kSetupApName);
  }

  sntp_set_time_sync_notification_cb(on_time_sync);
  configTzTime(g_settings.tz.c_str(), "pool.ntp.org", "time.google.com", "time.nist.gov");
}

void net_loop() {
  if (s_state == NetState::SetupPortal) s_wm.process();

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    if (s_state != NetState::Online) {
      s_state = NetState::Online;
      if (s_wm.getConfigPortalActive()) s_wm.stopConfigPortal();
      Serial.printf("[net] online at %s\n", WiFi.localIP().toString().c_str());
      // configTzTime() resets the TZ env var, so re-apply it after re-arming SNTP.
      configTzTime(g_settings.tz.c_str(), "pool.ntp.org", "time.google.com", "time.nist.gov");
    }
    if (!s_mdns_started && MDNS.begin(kHostname)) {
      MDNS.addService("http", "tcp", 80);
      s_mdns_started = true;
      // Firmware updates over WiFi (pio run -e ota -t upload). The flash holds two copies of the
      // firmware: the new one is written beside the running one and used from the next restart,
      // so a failed transfer leaves the clock as it was. The password keeps other devices on the
      // WiFi from flashing it.
      ArduinoOTA.setHostname(kHostname);
      ArduinoOTA.setMdnsEnabled(false);  // mDNS is already running, started just above
      ArduinoOTA.setPassword(OTA_PASSWORD);
      // Writing the flash holds up the memory the screen is fed from, and for the ~15 s of an
      // update the picture breaks up into noise. Turn the backlight off so the screen is just dark;
      // the clock restarts at the end (or night mode brings the light back if it fails).
      ArduinoOTA.onStart([] {
        Serial.println("[ota] update starting");
        display_set_brightness(0);
      });
      ArduinoOTA.onEnd([] { Serial.println("[ota] update done, restarting"); });
      ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] update failed (%u)\n", e); });
      ArduinoOTA.begin();
    }
    ArduinoOTA.handle();
  } else if (s_state == NetState::Online) {
    // Lost WiFi after being online. The ESP32 reconnects on its own; keep showing
    // the time, which stays correct from the internal clock in the meantime.
    s_state = NetState::Connecting;
  }
}

NetState net_state() { return s_state; }

bool net_time_valid() {
  if (s_time_synced) return true;
  // Also accept a clock that was set earlier (e.g. survived a soft reboot).
  time_t now = time(nullptr);
  return now > 1700000000;  // after Nov 2023
}

String net_ip() { return WiFi.localIP().toString(); }
