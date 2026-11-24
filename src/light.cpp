#include "light.h"

#include <Arduino.h>
#include <Wire.h>

static constexpr uint8_t kAddrs[] = {0x23, 0x5C};
static constexpr uint8_t kPowerOn = 0x01;
static constexpr uint8_t kContHiRes2 = 0x11;  // continuous, 0.5 lx steps: good in a dark room
static constexpr uint32_t kReadMs = 500;      // a hi-res conversion takes ~180 ms
static constexpr uint32_t kRetryMs = 30000;

// Dark below 3 lx, light again above 10 lx. Going dark waits a little so walking past a
// lamp or a car's headlights don't flip the screen back and forth; turning a lamp on
// wakes the screen quickly.
static constexpr float kDarkLux = 3.0f;
static constexpr float kLightLux = 10.0f;
static constexpr uint32_t kGoDarkMs = 20000;
static constexpr uint32_t kGoLightMs = 1500;

static uint8_t s_addr = 0;
static float s_lux = 0;
static bool s_have_lux = false;
static bool s_dark = false;
static uint32_t s_last_read_ms = 0, s_last_probe_ms = 0, s_cross_ms = 0;
static uint8_t s_fails = 0;

static bool send(uint8_t addr, uint8_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

static void probe() {
  s_last_probe_ms = millis();
  for (uint8_t a : kAddrs) {
    if (send(a, kPowerOn) && send(a, kContHiRes2)) {
      s_addr = a;
      s_fails = 0;
      s_last_read_ms = millis();  // first result is ready after one conversion
      Serial.printf("[light] BH1750 at 0x%02X\n", a);
      return;
    }
  }
}

void light_init() { probe(); }

void light_loop() {
  const uint32_t now = millis();
  if (!s_addr) {
    if (now - s_last_probe_ms > kRetryMs) probe();
    return;
  }
  if (now - s_last_read_ms < kReadMs) return;
  s_last_read_ms = now;

  if (Wire.requestFrom(s_addr, (uint8_t)2) != 2) {
    if (++s_fails >= 5) {  // unplugged: fall back to the night-hours schedule
      Serial.println("[light] sensor lost");
      s_addr = 0;
      s_have_lux = false;
      s_dark = false;
    }
    return;
  }
  const uint16_t raw = (Wire.read() << 8) | Wire.read();
  // All ones is a garbled read (seen 22 Nov with a loose ground on the sensor: lone 19,000 lx
  // spikes in a dark room that lit the screen to full every 20 s). Count it as a failed read.
  if (raw == 0xFFFF) {
    ++s_fails;
    return;
  }
  s_fails = 0;
  const float lux = raw / 2.4f;  // hi-res mode 2: raw / 1.2 / 2
  // Smooth, but follow big jumps (a lamp switched on) at once, once a second reading agrees.
  static float s_jump = -1;  // a big jump waiting for confirmation
  const bool jump = s_have_lux && lux > s_lux * 3 + 5;
  if (jump) {
    const bool confirmed = s_jump >= 0 && lux > s_jump * 0.5f && lux < s_jump * 2;
    s_jump = lux;
    if (!confirmed) return;
  }
  s_jump = -1;
  if (!s_have_lux || jump)
    s_lux = lux;
  else
    s_lux += (lux - s_lux) * 0.3f;
  if (!s_have_lux) s_dark = s_lux < kDarkLux;
  s_have_lux = true;

  // Change state only after the reading has stayed past the threshold for a while.
  const bool crossing = s_dark ? (s_lux > kLightLux) : (s_lux < kDarkLux);
  if (!crossing) {
    s_cross_ms = 0;
  } else {
    if (!s_cross_ms) s_cross_ms = now ? now : 1;
    if (now - s_cross_ms >= (s_dark ? kGoLightMs : kGoDarkMs)) {
      s_dark = !s_dark;
      s_cross_ms = 0;
    }
  }
}

bool light_present() { return s_addr != 0 && s_have_lux; }
float light_lux() { return light_present() ? s_lux : 0; }
bool light_is_dark() { return light_present() && s_dark; }
