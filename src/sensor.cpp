#include "sensor.h"

#include <Arduino.h>
#include <Wire.h>

#include "board.h"

enum class Kind { None, SHT4x, SHT3x, AM2302 };

// Sensirion SHT4x (SHT40/41/45) and SHT3x (SHT30/31/35) both sit at 0x44, or 0x45 with
// the ADDR pin high. They take different commands, so detection does a full test reading
// of each kind and checks its CRC.
static constexpr uint8_t kAddrs[] = {0x44, 0x45};
static constexpr uint8_t kSht4xMeasure = 0xFD;      // high repeatability, no heater
static constexpr uint16_t kSht3xMeasure = 0x2400;   // single shot, high repeatability, no clock stretch
static constexpr uint32_t kSht4xWaitMs = 10;        // datasheet max 8.3 ms
static constexpr uint32_t kSht3xWaitMs = 17;        // datasheet max 15.5 ms
static uint8_t s_addr = 0x44;
static constexpr uint32_t kReadIntervalMs = 5000;  // AM2302 needs at least 2 s between reads
static constexpr uint32_t kRetryIntervalMs = 30000;

static Kind s_kind = Kind::None;
static int s_am_failures = 0;
static IndoorReading s_latest;
static float s_offset_c = 0;

// Smoothed values that feed the history buffer.
static float s_avg_t = 0, s_avg_h = 0;
static int s_avg_n = 0;

static float s_hist_t[kHistoryPoints];
static float s_hist_h[kHistoryPoints];
static int s_hist_head = 0;   // next slot to write
static int s_hist_count = 0;
static uint32_t s_last_hist_ms = 0;

static uint32_t s_last_read_ms = 0;
static bool s_measuring = false;
static uint32_t s_measure_started_ms = 0;

static uint8_t crc8(const uint8_t *data, int len) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

static bool start_measurement(Kind kind, uint8_t addr) {
  Wire.beginTransmission(addr);
  if (kind == Kind::SHT3x) {
    Wire.write(kSht3xMeasure >> 8);
    Wire.write(kSht3xMeasure & 0xFF);
  } else {
    Wire.write(kSht4xMeasure);
  }
  return Wire.endTransmission() == 0;
}

static uint32_t measure_wait_ms(Kind kind) { return kind == Kind::SHT3x ? kSht3xWaitMs : kSht4xWaitMs; }

// Reads a finished measurement. Returns 0 if the sensor didn't answer, -1 on a bad CRC,
// 1 with t (no offset) and h filled in.
static int read_measurement(Kind kind, uint8_t addr, float &t, float &h) {
  uint8_t buf[6];
  if (Wire.requestFrom(addr, (uint8_t)6) != 6) return 0;
  for (int i = 0; i < 6; i++) buf[i] = Wire.read();
  if (crc8(buf, 2) != buf[2] || crc8(buf + 3, 2) != buf[5]) return -1;
  const uint16_t raw_t = (buf[0] << 8) | buf[1];
  const uint16_t raw_h = (buf[3] << 8) | buf[4];
  t = -45.0f + 175.0f * raw_t / 65535.0f;  // same formula for both families
  h = kind == Kind::SHT3x ? 100.0f * raw_h / 65535.0f : -6.0f + 125.0f * raw_h / 65535.0f;
  h = constrain(h, 0.0f, 100.0f);
  return 1;
}

// ---------- AM2302 / DHT22 (one-wire protocol, bit-banged) ----------

// Waits while the pin is at `level`; returns how long that took in microseconds, or -1 on timeout.
static int wait_while(int level, int timeout_us) {
  const uint32_t start = micros();
  while (digitalRead(PIN_AM2302) == level) {
    if ((int)(micros() - start) > timeout_us) return -1;
  }
  return (int)(micros() - start);
}

static bool am2302_read(float &t, float &h) {
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  uint8_t d[5] = {0};
  // Start signal: hold the line low for >1 ms, then let it float up and listen.
  pinMode(PIN_AM2302, OUTPUT);
  digitalWrite(PIN_AM2302, LOW);
  delayMicroseconds(1100);
  bool ok = true;
  // The reply takes about 5 ms; keep this core's interrupts off so the timing holds.
  portENTER_CRITICAL(&mux);
  pinMode(PIN_AM2302, INPUT_PULLUP);
  if (wait_while(HIGH, 100) < 0 || wait_while(LOW, 100) < 0 || wait_while(HIGH, 100) < 0) ok = false;
  for (int i = 0; ok && i < 40; i++) {
    // Each bit: ~50 us low, then high for ~27 us (0) or ~70 us (1).
    if (wait_while(LOW, 80) < 0) {
      ok = false;
      break;
    }
    const int high_us = wait_while(HIGH, 100);
    if (high_us < 0) {
      ok = false;
      break;
    }
    d[i / 8] = (d[i / 8] << 1) | (high_us > 45 ? 1 : 0);
  }
  portEXIT_CRITICAL(&mux);
  pinMode(PIN_AM2302, INPUT_PULLUP);
  if (!ok || (uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) return false;
  h = ((d[0] << 8) | d[1]) / 10.0f;
  t = (((d[2] & 0x7F) << 8) | d[3]) / 10.0f;
  if (d[2] & 0x80) t = -t;
  return h <= 100.0f;
}

// ---------- detection ----------

static void store_reading(float t, float h);

// Takes one blocking test reading; on success it's stored so the screen fills in at once.
static bool sht_probe(Kind kind, uint8_t addr) {
  if (!start_measurement(kind, addr)) return false;
  delay(measure_wait_ms(kind) + 2);
  float t, h;
  if (read_measurement(kind, addr, t, h) != 1) return false;
  s_addr = addr;
  store_reading(t + s_offset_c, h);
  return true;
}

static Kind probe() {
  for (uint8_t a : kAddrs) {
    if (sht_probe(Kind::SHT4x, a)) return Kind::SHT4x;
    if (sht_probe(Kind::SHT3x, a)) return Kind::SHT3x;
  }
  pinMode(PIN_AM2302, INPUT_PULLUP);
  float t, h;
  // (If the AM2302 is still waking up after power-on, the next retry finds it.)
  if (am2302_read(t, h)) {
    store_reading(t + s_offset_c, h);
    return Kind::AM2302;
  }
  return Kind::None;
}

const char *sensor_name() {
  switch (s_kind) {
    case Kind::SHT4x: return "SHT4x";
    case Kind::SHT3x: return "SHT3x";
    case Kind::AM2302: return "AM2302";
    default: return "none";
  }
}

void sensor_init() {
  s_kind = probe();
  static const char *kNone = "no SHT4x, SHT3x or AM2302 found (check SDA IO19 / SCL IO20 / 3V3 / GND)";
  Serial.printf("[sensor] %s\n", s_kind == Kind::None ? kNone : sensor_name());
  // Read right away; if nothing answered, look again in 3 s (an AM2302 can be slow to wake).
  s_last_read_ms = millis() - (s_kind == Kind::None ? kRetryIntervalMs - 3000 : kReadIntervalMs);
  s_last_hist_ms = millis();
}

static void push_history(float t, float h) {
  s_hist_t[s_hist_head] = t;
  s_hist_h[s_hist_head] = h;
  s_hist_head = (s_hist_head + 1) % kHistoryPoints;
  if (s_hist_count < kHistoryPoints) s_hist_count++;
}

static void finish_measurement() {
  float t, h;
  const int r = read_measurement(s_kind, s_addr, t, h);
  if (r == 0) {  // unplugged: go back to looking for a sensor
    s_kind = Kind::None;
    s_latest.valid = false;
  } else if (r == 1) {
    store_reading(t + s_offset_c, h);
  }  // bad CRC: keep the previous reading
}

static void store_reading(float t, float h) {
  s_latest.valid = true;
  s_latest.temp_c = t;
  s_latest.humidity = h;

  s_avg_t += t;
  s_avg_h += h;
  s_avg_n++;
  // First sample goes into history right away so the graph isn't empty after boot.
  if (s_hist_count == 0 || millis() - s_last_hist_ms >= kHistoryIntervalMs) {
    push_history(s_avg_t / s_avg_n, s_avg_h / s_avg_n);
    s_avg_t = s_avg_h = 0;
    s_avg_n = 0;
    s_last_hist_ms = millis();
  }
}

void sensor_loop() {
  const uint32_t now = millis();
  if (s_kind == Kind::None) {
    if (now - s_last_read_ms >= kRetryIntervalMs) {
      s_last_read_ms = now;
      s_kind = probe();
      if (s_kind != Kind::None) Serial.printf("[sensor] %s connected\n", sensor_name());
    }
    return;
  }
  if (s_kind == Kind::AM2302) {
    if (now - s_last_read_ms < kReadIntervalMs) return;
    s_last_read_ms = now;
    float t, h;
    if (am2302_read(t, h)) {
      s_am_failures = 0;
      store_reading(t + s_offset_c, h);
    } else if (++s_am_failures >= 5) {  // unplugged: go back to looking for a sensor
      s_am_failures = 0;
      s_kind = Kind::None;
      s_latest.valid = false;
    }
    return;
  }
  // Start a measurement, then collect it ~10 ms later without blocking the UI.
  if (!s_measuring && now - s_last_read_ms >= kReadIntervalMs) {
    s_last_read_ms = now;
    if (start_measurement(s_kind, s_addr)) {
      s_measuring = true;
      s_measure_started_ms = now;
    } else {
      s_kind = Kind::None;
      s_latest.valid = false;
    }
  } else if (s_measuring && now - s_measure_started_ms >= measure_wait_ms(s_kind)) {
    s_measuring = false;
    finish_measurement();
  }
}

bool sensor_present() { return s_kind != Kind::None; }
IndoorReading sensor_latest() { return s_latest; }

static int copy_history(const float *src, float *out, int max_points) {
  const int n = min(s_hist_count, max_points);
  const int start = (s_hist_head - n + kHistoryPoints) % kHistoryPoints;
  for (int i = 0; i < n; i++) out[i] = src[(start + i) % kHistoryPoints];
  return n;
}

int sensor_history_temp(float *out, int max_points) { return copy_history(s_hist_t, out, max_points); }
int sensor_history_humidity(float *out, int max_points) { return copy_history(s_hist_h, out, max_points); }

void sensor_set_temp_offset(float offset_c) { s_offset_c = offset_c; }
