#include "nightmode.h"

#include <Arduino.h>
#include <math.h>
#include <time.h>

#include "alarm.h"
#include "display.h"
#include "light.h"
#include "net.h"
#include "settings.h"
#include "sunrise.h"
#include "ui.h"
#include "ui_diag.h"

// A tap at night brightens to at most this, so it doesn't glare in a dark room.
static constexpr uint8_t kNightTapMaxBrightness = 40;
static constexpr uint32_t kFadeStepMs = 15;

static float s_level = -1;  // current backlight %, faded toward the target
static uint32_t s_last_step_ms = 0;
static bool s_is_night = false;

static bool in_night_window() {
  if (!net_time_valid()) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  const int m = t.tm_hour * 60 + t.tm_min;
  const int start = g_settings.night_start_min, end = g_settings.night_end_min;
  if (start == end) return false;
  // The window usually wraps past midnight (e.g. 22:00 to 07:00).
  return start < end ? (m >= start && m < end) : (m >= start || m < end);
}

// With a light sensor, daytime brightness follows the room: full in a bright room, down
// to about a third in a dim one (reading lamp), on a log scale like the eye.
static float room_factor() {
  if (!light_present()) return 1.0f;
  const float lux = constrain(light_lux(), 5.0f, 400.0f);
  return 0.35f + 0.65f * log10f(lux / 5.0f) / log10f(400.0f / 5.0f);
}

void nightmode_loop() {
  const bool tapped = display_ms_since_touch() < g_settings.tap_wake_secs * 1000UL;
  // A light sensor decides night by the room going dark; without one, the night hours do.
  if (!g_settings.night_enabled)
    s_is_night = false;
  else if (light_present())
    // While a tap has the screen lit at night, its own glow mustn't count as a lamp.
    s_is_night = light_is_dark() || (s_is_night && tapped);
  else
    s_is_night = in_night_window();
  // A ringing alarm lights the screen fully so Snooze and Stop are easy to see.
  const bool ringing = alarm_state() == AlarmState::Ringing;
  const bool diag = ui_diag_is_open();
  // A screen sunrise brings the backlight up with it, from night level to full day.
  const float sun = (ringing || diag) ? -1 : sunrise_progress();
  const bool dim = s_is_night && !tapped && !ringing && !diag && sun < 0;

  uint8_t target = max<int>(5, lroundf(g_settings.day_brightness * room_factor()));
  if (ringing)
    target = g_settings.day_brightness;
  else if (sun >= 0)
    target = lroundf(g_settings.night_brightness + (g_settings.day_brightness - g_settings.night_brightness) * sun * sun);
  else if (dim)
    target = g_settings.night_brightness;
  else if (s_is_night && !ringing)
    target = min(g_settings.day_brightness, kNightTapMaxBrightness);

  // The hardware check screen can set the backlight directly.
  if (ui_diag_brightness() >= 0) target = ui_diag_brightness();

  ui_set_night(dim);
  display_set_dimmed(dim);

  // Fade: quick to brighten after a tap, slow and gentle when going dark.
  const uint32_t now = millis();
  if (s_level < 0) s_level = target;
  if (now - s_last_step_ms < kFadeStepMs) return;
  s_last_step_ms = now;
  if (s_level < target)
    s_level = min<float>(target, s_level + 4.0f);
  else if (s_level > target)
    s_level = max<float>(target, s_level - 0.5f);
  const uint8_t pct = (uint8_t)lroundf(s_level);
  if (pct != display_get_brightness()) display_set_brightness(pct);
}

bool nightmode_is_night() { return s_is_night; }
