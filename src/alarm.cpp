#include "alarm.h"

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

#include "net.h"
#include "settings.h"

static constexpr uint32_t kMaxRingMs = 30UL * 60 * 1000;  // give up after 30 min of ringing
static constexpr uint8_t kStoreVersion = 2;  // 2 added sunrise_min
// After a power cut, an alarm that came due while the clock was off still rings if it's no
// more than this late (the time comes back from the internet within seconds of WiFi).
static constexpr long kCatchUpMinutes = 30;

static Alarm s_alarms[kMaxAlarms];
static AlarmState s_state = AlarmState::Idle;
static int s_ringing = -1;
static uint32_t s_ring_started_ms = 0;
static time_t s_snooze_until = 0;
static long s_last_checked_minute = -1;  // epoch minute last examined
static time_t s_ring_started = 0;        // wall-clock time the current alarm started ringing
static bool s_restored = false;          // runtime state read back from flash yet?

static const char *kDayShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// ---------- storage ----------

static uint32_t s_revision = 1;

static void save() {
  s_revision++;  // every change to the list goes through here
  Preferences p;
  if (!p.begin("alarms", false)) return;
  p.putUChar("ver", kStoreVersion);
  p.putBytes("list", s_alarms, sizeof(s_alarms));
  p.end();
}

// The layout saved by version 1 (before sunrise), so alarms survive the update.
struct AlarmV1 {
  bool used, enabled;
  uint8_t hour, minute, days;
  AlarmSound sound;
  uint8_t volume, snooze_min;
  bool ramp, skip_next;
};

static void load() {
  Preferences p;
  if (!p.begin("alarms", true)) return;
  const uint8_t ver = p.getUChar("ver", 0);
  const size_t len = p.getBytesLength("list");
  if (ver == kStoreVersion && len == sizeof(s_alarms)) {
    p.getBytes("list", s_alarms, sizeof(s_alarms));
  } else if (ver == 1 && len == sizeof(AlarmV1) * kMaxAlarms) {
    AlarmV1 old[kMaxAlarms];
    p.getBytes("list", old, sizeof(old));
    for (int i = 0; i < kMaxAlarms; i++) {
      Alarm &a = s_alarms[i];
      a.used = old[i].used;
      a.enabled = old[i].enabled;
      a.hour = old[i].hour;
      a.minute = old[i].minute;
      a.days = old[i].days;
      a.sound = old[i].sound;
      a.volume = old[i].volume;
      a.snooze_min = old[i].snooze_min;
      a.ramp = old[i].ramp;
      a.skip_next = old[i].skip_next;
      a.sunrise_min = 0;
    }
    Serial.println("[alarm] alarms carried over from the older format");
  }
  p.end();
}

void alarm_init() { load(); }

// ---------- surviving a power cut ----------
// There's no battery, so a power cut restarts the clock. The alarm list is saved above; this
// saves what's happening right now (ringing or snoozed, and which minute was last checked) so
// that after a restart the clock can pick up where it left off. "seen" changes once a minute;
// flash wear levelling spreads that out, good for many decades.

static void save_runtime() {
  if (!s_restored) return;  // don't overwrite what's saved before it's been read back
  Preferences p;
  if (!p.begin("alarmrt", false)) return;
  p.putUInt("seen", (uint32_t)s_last_checked_minute);
  p.putUChar("st", (uint8_t)s_state);
  p.putChar("idx", (int8_t)s_ringing);
  p.putUInt("t", (uint32_t)(s_state == AlarmState::Snoozed ? s_snooze_until : s_ring_started));
  p.end();
}

static void start_ringing(int i, bool ramp);

// Runs once, when the time is first known after a restart.
static void restore(time_t now) {
  s_restored = true;
  const long minute = (long)(now / 60);
  long seen = 0;
  uint8_t st = 0;
  int idx = -1;
  time_t t = 0;
  Preferences p;
  if (p.begin("alarmrt", true)) {
    seen = (long)p.getUInt("seen", 0);
    st = p.getUChar("st", 0);
    idx = p.getChar("idx", -1);
    t = (time_t)p.getUInt("t", 0);
    p.end();
  }

  // Look back over the minutes the clock was off, but no further than kCatchUpMinutes. With
  // nothing saved (first start), skip the minute already underway, as before.
  if (seen > 0 && seen <= minute)
    s_last_checked_minute = max(seen, minute - kCatchUpMinutes - 1);
  else
    s_last_checked_minute = minute;

  // Was an alarm ringing or snoozed when the power went?
  const bool valid = idx >= 0 && idx < kMaxAlarms && s_alarms[idx].used;
  if (valid && st == (uint8_t)AlarmState::Ringing && now - t < (time_t)(kMaxRingMs / 1000)) {
    start_ringing(idx, false);
    s_ring_started = t;  // still gives up 30 min after it first rang
    s_ring_started_ms = millis() - (uint32_t)(now - t) * 1000;
    Serial.println("[alarm] was ringing when the power went; ringing again");
  } else if (valid && st == (uint8_t)AlarmState::Snoozed) {
    if (t > now) {
      s_state = AlarmState::Snoozed;
      s_ringing = idx;
      s_snooze_until = t;
      Serial.println("[alarm] still snoozed after the power cut");
    } else if (now - t <= kCatchUpMinutes * 60) {
      start_ringing(idx, false);  // the snooze ran out while the power was off
    }
  }
  save_runtime();
}

const Alarm &alarm_get(int i) { return s_alarms[constrain(i, 0, kMaxAlarms - 1)]; }

void alarm_set(int i, const Alarm &a) {
  if (i < 0 || i >= kMaxAlarms) return;
  s_alarms[i] = a;
  s_alarms[i].used = true;
  save();
}

int alarm_add() {
  for (int i = 0; i < kMaxAlarms; i++) {
    if (!s_alarms[i].used) {
      s_alarms[i] = Alarm();
      s_alarms[i].used = true;
      save();
      return i;
    }
  }
  return -1;
}

void alarm_remove(int i) {
  if (i < 0 || i >= kMaxAlarms) return;
  if (s_ringing == i) alarm_stop();
  s_alarms[i] = Alarm();
  save();
}

uint32_t alarm_revision() { return s_revision; }

int alarm_count_used() {
  int n = 0;
  for (auto &a : s_alarms) n += a.used;
  return n;
}

// ---------- ringing ----------

static void start_ringing(int i, bool ramp) {
  const Alarm &a = s_alarms[i];
  s_ringing = i;
  s_state = AlarmState::Ringing;
  s_ring_started_ms = millis();
  s_ring_started = time(nullptr);
  audio_play(a.sound, a.volume, ramp);
  Serial.printf("[alarm] ringing #%d %02d:%02d\n", i, a.hour, a.minute);
  save_runtime();
}

void alarm_snooze() {
  if (s_state != AlarmState::Ringing) return;
  audio_stop();
  s_state = AlarmState::Snoozed;
  s_snooze_until = time(nullptr) + s_alarms[s_ringing].snooze_min * 60;
  save_runtime();
}

void alarm_stop() {
  audio_stop();
  s_state = AlarmState::Idle;
  s_ringing = -1;
  save_runtime();
}

AlarmState alarm_state() { return s_state; }
int alarm_ringing_index() { return s_ringing; }

int alarm_snooze_minutes_left() {
  if (s_state != AlarmState::Snoozed) return 0;
  const long left = (long)(s_snooze_until - time(nullptr));
  return left <= 0 ? 0 : (int)((left + 59) / 60);
}

// Does alarm `a` go off at this local minute?
static bool due(const Alarm &a, const struct tm &t) {
  if (!a.used || !a.enabled || a.hour != t.tm_hour || a.minute != t.tm_min) return false;
  return a.days == 0 || (a.days & (1 << t.tm_wday));
}

bool alarm_rings_at(int i, time_t at) {
  if (i < 0 || i >= kMaxAlarms) return false;
  const Alarm &a = s_alarms[i];
  struct tm t;
  localtime_r(&at, &t);
  return due(a, t) && !a.skip_next;
}

// Handle the alarms due at one epoch minute. `late` = the clock was off (or busy) then.
static void check_minute(long minute, bool late) {
  const time_t at = (time_t)minute * 60;
  struct tm t;
  localtime_r(&at, &t);
  for (int i = 0; i < kMaxAlarms; i++) {
    Alarm &a = s_alarms[i];
    if (!due(a, t)) continue;
    if (a.skip_next) {
      a.skip_next = false;
      if (a.days == 0) a.enabled = false;
      save();
      continue;
    }
    if (a.days == 0) {  // one-time alarm: turn itself off
      a.enabled = false;
      save();
    }
    if (late) Serial.printf("[alarm] #%d came due while the clock was off; ringing now\n", i);
    if (s_state == AlarmState::Idle) start_ringing(i, a.ramp);
  }
}

void alarm_loop() {
  if (!net_time_valid()) return;
  const time_t now = time(nullptr);
  if (!s_restored) restore(now);

  // After a snooze the alarm comes back at full volume; the gentle ramp was the first round.
  if (s_state == AlarmState::Snoozed && now >= s_snooze_until) start_ringing(s_ringing, false);
  if (s_state == AlarmState::Ringing && millis() - s_ring_started_ms > kMaxRingMs) alarm_stop();

  // Check each minute once, as it starts, plus any minutes missed while the power was off or
  // the clock jumped forward (up to kCatchUpMinutes back).
  const long minute = (long)(now / 60);
  if (minute == s_last_checked_minute) return;
  if (minute < s_last_checked_minute) {  // the clock was set back: don't ring anything twice
    s_last_checked_minute = minute;
    save_runtime();
    return;
  }
  const long from = max(s_last_checked_minute + 1, minute - kCatchUpMinutes);
  s_last_checked_minute = minute;
  for (long m = from; m <= minute; m++) check_minute(m, m < minute);
  save_runtime();
}

// ---------- labels ----------

void alarm_format_time(int hour, int minute, char *out, size_t n) {
  if (g_settings.use_24h) {
    snprintf(out, n, "%02d:%02d", hour, minute);
  } else {
    const int h12 = hour % 12 == 0 ? 12 : hour % 12;
    snprintf(out, n, "%d:%02d %s", h12, minute, hour < 12 ? "AM" : "PM");
  }
}

void alarm_format_days(uint8_t days, char *out, size_t n) {
  days &= 0x7F;
  if (days == 0) {
    snprintf(out, n, "Once");
  } else if (days == 0x7F) {
    snprintf(out, n, "Every day");
  } else if (days == 0b0111110) {
    snprintf(out, n, "Weekdays");
  } else if (days == 0b1000001) {
    snprintf(out, n, "Weekends");
  } else {
    out[0] = 0;
    for (int d = 1; d <= 7; d++) {  // start the week on Monday
      const int wd = d % 7;
      if (!(days & (1 << wd))) continue;
      if (out[0]) strlcat(out, " ", n);
      strlcat(out, kDayShort[wd], n);
    }
  }
}

bool alarm_next_label(char *out, size_t n) {
  if (s_state == AlarmState::Snoozed) {
    snprintf(out, n, "Snoozed, %d min", alarm_snooze_minutes_left());
    return true;
  }
  if (!net_time_valid()) return false;

  const time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  const int now_min = t.tm_hour * 60 + t.tm_min;

  // Search the next 8 days for the soonest ring.
  int best_delta = INT32_MAX, best = -1, best_day = 0;
  for (int i = 0; i < kMaxAlarms; i++) {
    const Alarm &a = s_alarms[i];
    if (!a.used || !a.enabled) continue;
    const int at = a.hour * 60 + a.minute;
    bool skipped_one = !a.skip_next;  // with skip_next, ignore the first match
    for (int d = 0; d < 8; d++) {
      const int wd = (t.tm_wday + d) % 7;
      const int delta = d * 1440 + at - now_min;
      if (delta <= 0) continue;
      if (a.days != 0 && !(a.days & (1 << wd))) continue;
      if (!skipped_one) {
        skipped_one = true;
        if (a.days == 0) break;  // a skipped one-time alarm never rings
        continue;
      }
      if (delta < best_delta) {
        best_delta = delta;
        best = i;
        best_day = wd;
      }
      break;
    }
  }
  if (best < 0) return false;

  char hm[16];
  alarm_format_time(s_alarms[best].hour, s_alarms[best].minute, hm, sizeof(hm));
  if (best_delta < 24 * 60)
    snprintf(out, n, "%s", hm);
  else
    snprintf(out, n, "%s %s", kDayShort[best_day], hm);
  return true;
}
