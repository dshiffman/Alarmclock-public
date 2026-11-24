#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board.h"

// Timing values from Freenove's examples for the 4.3" IPS panel.
// The frame lives in PSRAM. When WiFi or a flash write keeps PSRAM busy, the panel misses pixels and
// the picture jumps sideways for a frame. A bounce buffer in internal RAM (10 lines x 2, about 32 KB)
// feeds the panel instead and fixes that.
static constexpr size_t kBounceBufferPx = LCD_WIDTH * 10;
static Arduino_ESP32RGBPanel *s_panel = new Arduino_ESP32RGBPanel(
    PIN_LCD_DE, PIN_LCD_VSYNC, PIN_LCD_HSYNC, PIN_LCD_PCLK,
    PIN_LCD_R0, PIN_LCD_R1, PIN_LCD_R2, PIN_LCD_R3, PIN_LCD_R4,
    PIN_LCD_G0, PIN_LCD_G1, PIN_LCD_G2, PIN_LCD_G3, PIN_LCD_G4, PIN_LCD_G5,
    PIN_LCD_B0, PIN_LCD_B1, PIN_LCD_B2, PIN_LCD_B3, PIN_LCD_B4,
    0 /* hsync_polarity */, 4 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
    0 /* vsync_polarity */, 4 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */,
    1 /* pclk_active_neg */, LCD_PCLK_HZ, false /* useBigEndian */, 0 /* de_idle_high */, 0 /* pclk_idle_high */,
    kBounceBufferPx);

static Arduino_RGB_Display *s_gfx =
    new Arduino_RGB_Display(LCD_WIDTH, LCD_HEIGHT, s_panel, 0 /* rotation */, true /* auto_flush */);

static TAMC_GT911 s_touch(PIN_I2C_SDA, PIN_I2C_SCL, PIN_TOUCH_INT, PIN_TOUCH_RST, LCD_WIDTH, LCD_HEIGHT);

// LVGL draw buffers, in PSRAM. Internal RAM is what WiFi and the secure weather downloads need:
// with 48-line buffers there, phones couldn't join the setup network (8 KB left), and even at 16
// lines a download left only 15 KB. Drawing into PSRAM is a little slower, which a clock that
// redraws a few digits a minute doesn't notice.
static constexpr uint32_t kBufLines = 16;
static constexpr uint32_t kBacklightHz = 5000;
static constexpr uint8_t kBacklightBits = 10;

static uint8_t s_brightness = 0;
static uint32_t s_last_touch_ms = 0;
static bool s_dimmed = false;
static bool s_swallowing = false;  // current press started while dimmed

static volatile uint32_t s_flushed_px = 0;  // pixels drawn since boot (for display_draw_rate)
static uint32_t s_log_until_ms = 0;          // list redrawn areas on serial until then

void display_log_flushes(uint32_t ms) { s_log_until_ms = millis() + ms; }

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  s_flushed_px += w * h;
  if (s_log_until_ms && (int32_t)(s_log_until_ms - millis()) > 0)
    Serial.printf("[draw] %lu ms  x %d-%d  y %d-%d\n", (unsigned long)millis(), (int)area->x1, (int)area->x2,
                  (int)area->y1, (int)area->y2);
  s_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(px), w, h);
  lv_display_flush_ready(disp);
}

static uint32_t s_touch_log_until_ms = 0;  // serial 't': print what the touch panel reports
void display_log_touch(uint32_t ms) { s_touch_log_until_ms = millis() + ms; }

// Restart the shared I2C bus. A jolt on the sensor wiring (22 Nov: re-soldering a loose ground with
// the clock on) can leave the bus jammed: every read fails until a restart, the light reading
// freezes and touch reads as held down. Nine clock pulses let a sensor stuck mid-byte let go of
// the data line, then the driver starts afresh.
static void i2c_recover() {
  Serial.println("[i2c] bus jammed, restarting it");
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);
  }
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
}

static void touch_read_cb(lv_indev_t *, lv_indev_data_t *data) {
  s_touch.read();
  // The GT911 reports at most 5 points. A failed read comes back as all ones (15 points): treat it
  // as no touch, and restart the bus if reads keep failing for 2 seconds.
  static uint32_t s_bad_since = 0;
  const bool bad = s_touch.touches > 5;
  if (!bad) {
    s_bad_since = 0;
  } else if (!s_bad_since) {
    s_bad_since = millis() | 1;
  } else if (millis() - s_bad_since > 2000) {
    i2c_recover();
    s_bad_since = 0;
  }
  const bool pressed = !bad && s_touch.isTouched && s_touch.touches > 0;
  if (s_touch_log_until_ms && (int32_t)(s_touch_log_until_ms - millis()) > 0) {
    static uint32_t last = 0;
    if (millis() - last >= 200) {
      last = millis();
      if (pressed)
        Serial.printf("[touch] %d point(s), first at %d,%d\n", s_touch.touches, s_touch.points[0].x,
                      s_touch.points[0].y);
      else
        Serial.println("[touch] none");
    }
  }
  if (pressed) s_last_touch_ms = millis();
  if (pressed && s_dimmed && !s_swallowing) s_swallowing = true;
  if (!pressed) s_swallowing = false;
  if (s_swallowing) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (pressed) {
    // The panel's touch axes run opposite to the display's, as in Freenove's examples.
    int32_t x = (LCD_WIDTH - 1) - s_touch.points[0].x;
    int32_t y = (LCD_HEIGHT - 1) - s_touch.points[0].y;
    data->point.x = constrain(x, 0, LCD_WIDTH - 1);
    data->point.y = constrain(y, 0, LCD_HEIGHT - 1);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static uint32_t tick_cb() { return millis(); }

bool display_init() {
  // Keep the backlight off until the first frame is drawn.
  ledcAttach(PIN_BACKLIGHT, kBacklightHz, kBacklightBits);
  ledcWrite(PIN_BACKLIGHT, 0);

  if (!s_gfx->begin()) {
    Serial.println("[display] panel init failed");
    return false;
  }
  s_gfx->fillScreen(RGB565_BLACK);

  // GT911 starts Wire on the shared I2C pins.
  s_touch.begin();
  s_touch.setRotation(ROTATION_NORMAL);
  // 100 kHz: the bus runs through the sensor hub to four devices (touch, SHT, BH1750, SCD41), and
  // at 400 kHz the longer wiring gave read errors in the case (22 Nov).
  Wire.setClock(100000);

  lv_init();
  lv_tick_set_cb(tick_cb);

  const size_t buf_bytes = LCD_WIDTH * kBufLines * sizeof(uint16_t);
  void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf1 || !buf2) {
    Serial.println("[display] out of memory for draw buffers");
    return false;
  }

  lv_display_t *disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);

  s_last_touch_ms = millis();
  return true;
}

void display_set_brightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  s_brightness = percent;
  // Perceived brightness is roughly quadratic in duty cycle; this keeps low
  // settings usable at night instead of jumping straight from off to glaring.
  const uint32_t max_duty = (1u << kBacklightBits) - 1;
  uint32_t duty = (uint32_t)percent * percent * max_duty / 10000;
  if (percent > 0 && duty == 0) duty = 1;
  ledcWrite(PIN_BACKLIGHT, duty);
}

uint8_t display_get_brightness() { return s_brightness; }

uint32_t display_ms_since_touch() { return millis() - s_last_touch_ms; }

void display_poke() { s_last_touch_ms = millis(); }

void display_set_dimmed(bool dimmed) { s_dimmed = dimmed; }

// Average redraw rate since the last call, in screens per minute (one screen = 800 x 480). An idle
// clock redraws a little each minute; a stuck redraw loop shows up as hundreds.
float display_draw_rate() {
  static uint32_t last_px = 0, last_ms = 0;
  static float rate = 0;
  const uint32_t now = millis(), px = s_flushed_px;
  if (now - last_ms >= 1000) {
    rate = (float)(px - last_px) / (LCD_WIDTH * LCD_HEIGHT) * 60000.0f / (now - last_ms);
    last_px = px;
    last_ms = now;
  }
  return rate;
}
