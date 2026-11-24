// Bedroom alarm clock: main loop.
// Clock with indoor temperature/humidity, outdoor weather, sunrise/sunset, tides on
// double-tap, WiFi setup, network time, a night mode that dims until tapped, and alarms
// with an on-screen snooze.

#include <Arduino.h>
#include <lvgl.h>

#include "alarm.h"
#include "audio.h"
#include "board.h"
#include "diag.h"
#include "display.h"
#include "co2.h"
#include "light.h"
#include "net.h"
#include "nightmode.h"
#include "sensor.h"
#include "settings.h"
#include "sleep.h"
#include "sunrise.h"
#include "ui.h"
#include "weather.h"
#include "web.h"

static uint32_t s_last_ui_ms = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n[boot] alarm clock starting");

  settings_load();

  if (!display_init()) {
    Serial.println("[boot] display failed; halting");
    while (true) delay(1000);
  }
  audio_init();  // before the UI: the song's name goes into the alarm editor's sound list
  ui_init();
  lv_timer_handler();  // draw the first frame before turning the backlight on
  display_set_brightness(g_settings.day_brightness);

  sensor_set_temp_offset(g_settings.temp_offset_c);
  sensor_init();
  light_init();
  co2_init();

  alarm_init();

  // Show "Connecting to WiFi..." while autoConnect() runs (it can take ~20 s).
  ui_update();
  lv_timer_handler();
  net_init();
  weather_init();
  web_init();
  diag_print();  // hardware check on the serial monitor at 115200
}

void loop() {
  main_lock();  // the settings page's handlers wait for the pause at the end of each cycle
  diag_serial_loop();
  net_loop();
  sensor_loop();
  light_loop();
  co2_loop();
  alarm_loop();
  sleep_loop();
  sunrise_loop();

  const uint32_t now = millis();
  if (now - s_last_ui_ms >= 250) {
    s_last_ui_ms = now;
    ui_update();
  }
  nightmode_loop();

  const uint32_t idle_ms = lv_timer_handler();
  main_unlock();
  delay(constrain(idle_ms, 1, 5));  // at least 1 ms, so a waiting web request always gets a turn
}
