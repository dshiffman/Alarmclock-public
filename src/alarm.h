#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "audio.h"

// Up to 10 alarms, saved in flash. Each has a time, the days it repeats on (or none for
// a one-time alarm), a sound, volume, snooze length, gentle volume ramp, and "skip next".

static constexpr int kMaxAlarms = 10;

struct Alarm {
  bool used = false;
  bool enabled = true;
  uint8_t hour = 7, minute = 0;
  uint8_t days = 0b0111110;  // bit 0 = Sunday ... bit 6 = Saturday; 0 = once
  AlarmSound sound = AlarmSound::Chimes;
  uint8_t volume = 70;       // %
  uint8_t snooze_min = 9;
  bool ramp = true;          // start quiet and get louder
  bool skip_next = false;    // skip the next time it would ring, then carry on
  uint8_t sunrise_min = 0;   // screen sunrise this many minutes before it rings; 0 = off
};

static constexpr uint8_t kSunriseChoices[] = {0, 15, 20, 30};  // minutes (0 = off)

enum class AlarmState { Idle, Ringing, Snoozed };

void alarm_init();
void alarm_loop();  // call often from loop()

// Editing (from the touch screen or web page). Changes are saved immediately.
const Alarm &alarm_get(int i);
void alarm_set(int i, const Alarm &a);
int alarm_add();         // returns new index, or -1 if full
void alarm_remove(int i);
int alarm_count_used();
// Goes up whenever any alarm changes (from the screen, the web page, or by itself, e.g. a
// one-time alarm turning off). The web page sends it back with edits so a stale page can't
// overwrite a newer change.
uint32_t alarm_revision();

// Does alarm i ring at the local minute containing t (on, right day, not skipped)?
bool alarm_rings_at(int i, time_t t);

// Ringing control
AlarmState alarm_state();
int alarm_ringing_index();
void alarm_snooze();
void alarm_stop();
// Minutes left until a snoozed alarm rings again.
int alarm_snooze_minutes_left();

// Writes a short description of the next alarm (e.g. "Tue 6:30 AM") and returns true,
// or returns false when no alarm is on.
bool alarm_next_label(char *out, size_t n);

// "6:30 AM" / "06:30"
void alarm_format_time(int hour, int minute, char *out, size_t n);
// "Weekdays", "Every day", "Once", "Mon Wed Fri"...
void alarm_format_days(uint8_t days, char *out, size_t n);
