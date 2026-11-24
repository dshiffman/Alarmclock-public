#include "sunrise.h"

#include <Arduino.h>
#include <time.h>

#include "alarm.h"
#include "net.h"

static constexpr uint32_t kPreviewRiseMs = 60000;
static constexpr uint32_t kPreviewHoldMs = 10000;
static constexpr uint32_t kCheckMs = 500;  // the look-ahead below is a few hundred time lookups

static float s_progress = -1;
static int s_index = -1;
static time_t s_ring_at = 0;        // when the alarm being led up to rings
static time_t s_dismissed_at = 0;   // the ring time a tap turned off
static bool s_preview = false;
static uint32_t s_preview_start_ms = 0;
static uint32_t s_last_check_ms = 0;

void sunrise_preview_start() {
  s_preview = true;
  s_preview_start_ms = millis();
  s_progress = 0;
}

bool sunrise_previewing() { return s_preview; }

void sunrise_dismiss() {
  if (s_preview) {
    s_preview = false;
  } else if (s_index >= 0) {
    s_dismissed_at = s_ring_at;
  }
  s_progress = -1;
  s_index = -1;
}

float sunrise_progress() { return s_progress; }
int sunrise_alarm_index() { return s_preview ? -1 : s_index; }

void sunrise_loop() {
  // A ringing or snoozed alarm takes over the screen (and a preview gives way to it too).
  if (alarm_state() != AlarmState::Idle) {
    s_preview = false;
    s_progress = -1;
    s_index = -1;
    return;
  }
  if (s_preview) {
    const uint32_t e = millis() - s_preview_start_ms;
    if (e >= kPreviewRiseMs + kPreviewHoldMs) {
      s_preview = false;
      s_progress = -1;
    } else {
      s_progress = min(1.0f, (float)e / kPreviewRiseMs);
    }
    return;
  }

  if (millis() - s_last_check_ms < kCheckMs) return;
  s_last_check_ms = millis();
  if (!net_time_valid()) {
    s_progress = -1;
    s_index = -1;
    return;
  }

  // For each alarm with a sunrise, is it due to ring within its sunrise length?
  const time_t now = time(nullptr);
  const long minute = (long)(now / 60);
  float best = -1;
  int best_i = -1;
  time_t best_at = 0;
  for (int i = 0; i < kMaxAlarms; i++) {
    const Alarm &a = alarm_get(i);
    if (!a.used || !a.enabled || a.sunrise_min == 0) continue;
    for (int m = 1; m <= a.sunrise_min; m++) {
      const time_t at = (time_t)(minute + m) * 60;
      if (!alarm_rings_at(i, at)) continue;
      if (at == s_dismissed_at) break;
      const float p = 1.0f - (float)(at - now) / (a.sunrise_min * 60.0f);
      if (p > best) {
        best = max(0.0f, p);
        best_i = i;
        best_at = at;
      }
      break;
    }
  }
  if (best_i >= 0 && s_index < 0) Serial.printf("[sunrise] starting for alarm #%d\n", best_i);
  s_progress = best;
  s_index = best_i;
  s_ring_at = best_at;
}
