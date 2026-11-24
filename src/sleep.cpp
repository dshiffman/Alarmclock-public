#include "sleep.h"

#include <Arduino.h>

#include "audio.h"
#include "settings.h"

static constexpr uint32_t kFadeOutMs = 120000;  // the timer's last two minutes fade to silence

static bool s_active = false;
static uint32_t s_started_ms = 0;
static uint16_t s_timer_min = 0;  // for this session (0 = all night)

static float level_now() {
  const float v = g_settings.sleep_volume / 100.0f;
  float level = v * v;  // as for alarms: loudness follows the square of amplitude
  if (s_timer_min) {
    const uint32_t total = s_timer_min * 60000UL, elapsed = millis() - s_started_ms;
    const uint32_t left = elapsed >= total ? 0 : total - elapsed;
    if (left < kFadeOutMs) level *= (float)left / kFadeOutMs;
  }
  return level;
}

void sleep_start() {
  s_active = true;
  s_started_ms = millis();
  s_timer_min = g_settings.sleep_minutes;
  audio_sleep_play((SleepSound)g_settings.sleep_sound, level_now());
  if (!audio_sleep_playing()) s_active = false;  // an alarm sound is playing
  Serial.printf("[sleep] %s, %d %%, %s\n", sleep_sound_name((SleepSound)g_settings.sleep_sound),
                g_settings.sleep_volume, sleep_timer_label(s_timer_min));
}

void sleep_stop() {
  if (!s_active) return;
  s_active = false;
  audio_sleep_fade_out();
}

bool sleep_active() { return s_active; }

void sleep_loop() {
  if (!s_active) return;
  if (!audio_sleep_playing()) {  // an alarm (or a preview) took over
    s_active = false;
    return;
  }
  if (s_timer_min && millis() - s_started_ms >= s_timer_min * 60000UL) {
    s_active = false;
    audio_sleep_fade_out();
    Serial.println("[sleep] timer done");
    return;
  }
  static uint32_t last_ms = 0;
  if (millis() - last_ms < 200) return;
  last_ms = millis();
  audio_sleep_play((SleepSound)g_settings.sleep_sound, level_now());
}

void sleep_set_sound(SleepSound s) {
  g_settings.sleep_sound = (uint8_t)s;
  settings_save();
  if (s_active) audio_sleep_play(s, level_now());
}

void sleep_set_volume(uint8_t percent) {
  g_settings.sleep_volume = constrain(percent, 5, 100);
  settings_save();
}

void sleep_set_timer(uint16_t minutes) {
  g_settings.sleep_minutes = minutes;
  settings_save();
  if (s_active) {  // restart the countdown with the new length
    s_started_ms = millis();
    s_timer_min = minutes;
  }
}

int sleep_minutes_left() {
  if (!s_active || !s_timer_min) return -1;
  const uint32_t elapsed = millis() - s_started_ms, total = s_timer_min * 60000UL;
  return elapsed >= total ? 0 : (int)((total - elapsed + 59999) / 60000);
}

const char *sleep_timer_label(uint16_t minutes) {
  switch (minutes) {
    case 0: return "All night";
    case 15: return "15 min";
    case 30: return "30 min";
    case 60: return "1 hour";
    case 120: return "2 hours";
    default: return "";
  }
}
