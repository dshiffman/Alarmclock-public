#include "diag.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include "board.h"
#include "co2.h"
#include "display.h"
#include "light.h"
#include "net.h"
#include "sensor.h"

static const char *i2c_name(uint8_t a) {
  switch (a) {
    case 0x14: case 0x5D: return "touch (GT911)";
    case 0x44: case 0x45: return "temp/humidity (SHT)";
    case 0x23: case 0x5C: return "light (BH1750)";
    case 0x62: return "CO2 (SCD4x)";
    default: return "?";
  }
}

#define ADD(...) n += snprintf(out + n, n < len ? len - n : 0, __VA_ARGS__)

void diag_report(char *out, size_t len) {
  size_t n = 0;
  out[0] = 0;
  ADD("Built %s %s\n", __DATE__, __TIME__);

  ADD("I2C:");
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      ADD("%s 0x%02X %s", found ? "," : "", a, i2c_name(a));
      found++;
    }
  }
  if (!found) ADD(" nothing (touch chip should always show)");
  ADD("\n");

  const IndoorReading r = sensor_latest();
  if (sensor_present() && r.valid)
    ADD("Temp sensor: %s  %.1f C / %.1f F  %.0f %%RH\n", sensor_name(), r.temp_c, r.temp_c * 9 / 5 + 32, r.humidity);
  else
    ADD("Temp sensor: NOT FOUND (check SDA IO19 / SCL IO20 / 3V3 / GND)\n");

  if (light_present())
    ADD("Light sensor: BH1750  %.1f lux  (%s)\n", light_lux(), light_is_dark() ? "dark" : "light");
  else
    ADD("Light sensor: not found (optional; night mode uses night hours)\n");

  if (co2_present())
    ADD("CO2 sensor: %s  %d ppm%s  serial %012llX\n", co2_variant(), co2_ppm(), co2_high() ? " (stuffy)" : "",
        (unsigned long long)co2_serial());
  else if (co2_serial())
    ADD("CO2 sensor: %s found, waiting for the first reading (~30 s)\n", co2_variant());
  else
    ADD("CO2 sensor: not found (optional)\n");

  if (WiFi.status() == WL_CONNECTED)
    ADD("WiFi: %s  %d dBm  %s  time %s\n", WiFi.SSID().c_str(), WiFi.RSSI(), net_ip().c_str(), net_time_valid() ? "set" : "not set");
  else
    ADD("WiFi: not connected\n");

  ADD("Memory: heap %u KB free (lowest %u), PSRAM %u KB free, up %lu min",
      (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024),
      (unsigned)(ESP.getFreePsram() / 1024), (unsigned long)(millis() / 60000));
}

void diag_print() {
  static char buf[768];
  diag_report(buf, sizeof(buf));
  Serial.println("---- diagnostics ----");
  Serial.println(buf);
  Serial.println("---------------------");
}

void diag_serial_loop() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'd' || c == 'D') diag_print();
    if (c == 'f' || c == 'F') display_log_flushes(10000);  // list what gets redrawn for 10 s
    if (c == 't' || c == 'T') display_log_touch(15000);    // what the touch panel reports, 15 s
  }
}
