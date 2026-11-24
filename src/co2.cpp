#include "co2.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// Datasheet: Sensirion SCD4x v1.7 (copy in the project files). 16-bit commands; every data
// word comes back with a CRC byte.
static constexpr uint8_t kAddr = 0x62;
static constexpr uint16_t kStopPeriodic = 0x3f86;    // 500 ms
static constexpr uint16_t kStartLowPower = 0x21ac;   // a reading about every 30 s, ~3 mA
static constexpr uint16_t kDataReady = 0xe4b8;
static constexpr uint16_t kReadMeasurement = 0xec05;
static constexpr uint16_t kGetSerial = 0x3682;
static constexpr uint16_t kGetVariant = 0x202f;
static constexpr uint16_t kSetPressure = 0xe000;      // allowed while measuring

static constexpr uint32_t kRetryMs = 30000;   // look for the sensor again (plugged in later)
static constexpr uint32_t kPollMs = 5000;     // ask whether a new reading is ready
static constexpr uint32_t kStaleMs = 180000;  // no reading for 3 minutes: treat as gone
static constexpr int kHighOn = 1400, kHighOff = 1200;

enum class State { Absent, Stopping, Running };
static State s_state = State::Absent;
static uint32_t s_t = 0, s_last_ok_ms = 0;
static uint8_t s_fails = 0;
static int s_ppm = 0;
static bool s_high = false;
static uint64_t s_serial = 0;
static const char *s_variant = "none";
static float s_pressure_hpa = NAN, s_sent_hpa = NAN;

static uint8_t crc8(const uint8_t *d, int n) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1;
  }
  return crc;
}

static bool command(uint16_t cmd) {
  Wire.beginTransmission(kAddr);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool write_word(uint16_t cmd, uint16_t value) {
  const uint8_t v[2] = {(uint8_t)(value >> 8), (uint8_t)value};
  Wire.beginTransmission(kAddr);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xFF);
  Wire.write(v, 2);
  Wire.write(crc8(v, 2));
  return Wire.endTransmission() == 0;
}

// Sends a read command and fetches `n` words, checking each word's CRC.
static bool read_words(uint16_t cmd, uint16_t *out, int n) {
  if (!command(cmd)) return false;
  delay(1);  // max command duration for these reads
  const int len = n * 3;
  if (Wire.requestFrom(kAddr, (uint8_t)len) != len) return false;
  for (int i = 0; i < n; i++) {
    uint8_t b[3];
    for (uint8_t &x : b) x = Wire.read();
    if (crc8(b, 2) != b[2]) return false;
    out[i] = (b[0] << 8) | b[1];
  }
  return true;
}

static void lost(const char *why) {
  Serial.printf("[co2] sensor lost (%s)\n", why);
  s_state = State::Absent;
  s_t = millis();
  s_ppm = 0;
  s_high = false;
  s_sent_hpa = NAN;
}

static void probe() {
  s_t = millis();
  // It may still be measuring from before the ESP32 restarted: stop it so it takes commands.
  if (command(kStopPeriodic)) s_state = State::Stopping;
}

static void start() {
  uint16_t w[3];
  if (!read_words(kGetSerial, w, 3)) {
    s_state = State::Absent;
    s_t = millis();
    return;
  }
  s_serial = ((uint64_t)w[0] << 32) | ((uint32_t)w[1] << 16) | w[2];
  s_variant = "SCD4x";
  if (read_words(kGetVariant, w, 1)) {
    switch (w[0] >> 12) {
      case 0x0: s_variant = "SCD40"; break;
      case 0x1: s_variant = "SCD41"; break;
      case 0x5: s_variant = "SCD43"; break;
    }
  }
  if (!command(kStartLowPower)) {
    s_state = State::Absent;
    s_t = millis();
    return;
  }
  Serial.printf("[co2] %s at 0x62, serial %012llX; first reading in ~30 s\n", s_variant,
                (unsigned long long)s_serial);
  s_state = State::Running;
  s_t = s_last_ok_ms = millis();
  s_fails = 0;
}

void co2_init() { probe(); }

void co2_set_pressure_hpa(float hpa) {
  if (hpa >= 700 && hpa <= 1200) s_pressure_hpa = hpa;  // the sensor takes 700-1,200 hPa
}

void co2_loop() {
  const uint32_t now = millis();
  switch (s_state) {
    case State::Absent:
      if (now - s_t > kRetryMs) probe();
      return;
    case State::Stopping:
      if (now - s_t >= 500) start();
      return;
    case State::Running:
      break;
  }
  if (now - s_t < kPollMs) return;
  s_t = now;

  if (!isnan(s_pressure_hpa) && !(fabsf(s_pressure_hpa - s_sent_hpa) < 1)) {
    if (write_word(kSetPressure, (uint16_t)lroundf(s_pressure_hpa))) s_sent_hpa = s_pressure_hpa;
  }

  uint16_t ready;
  if (!read_words(kDataReady, &ready, 1)) {
    if (++s_fails >= 5) lost("no answer");
    return;
  }
  s_fails = 0;
  if ((ready & 0x07FF) == 0) {
    if (now - s_last_ok_ms > kStaleMs) lost("no new readings");
    return;
  }
  uint16_t m[3];
  if (!read_words(kReadMeasurement, m, 3) || m[0] == 0) return;  // 0 = still warming up
  s_ppm = m[0];
  s_last_ok_ms = now;
  if (s_ppm >= kHighOn) s_high = true;
  else if (s_ppm < kHighOff) s_high = false;
}

bool co2_present() { return s_state == State::Running && s_ppm > 0; }
int co2_ppm() { return co2_present() ? s_ppm : 0; }
bool co2_high() { return co2_present() && s_high; }
const char *co2_variant() { return s_state == State::Absent ? "none" : s_variant; }
uint64_t co2_serial() { return s_state == State::Absent ? 0 : s_serial; }
