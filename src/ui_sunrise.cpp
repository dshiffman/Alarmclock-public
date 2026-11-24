#include "ui_sunrise.h"

#include <Arduino.h>
#include <time.h>

#include "alarm.h"
#include "board.h"
#include "net.h"
#include "settings.h"
#include "sunrise.h"

LV_FONT_DECLARE(font_clock);
LV_FONT_DECLARE(font_md);
LV_FONT_DECLARE(font_sm);

// Colours along the way, like the sky: night, deep red, orange, warm white.
struct Stop {
  float at;
  uint32_t rgb;
};
static const Stop kSky[] = {{0.0f, 0x000000}, {0.25f, 0x3A0500}, {0.5f, 0xB32A00}, {0.75f, 0xFF8A1E}, {1.0f, 0xFFE2B0}};

static lv_obj_t *s_panel, *s_time, *s_note;
static uint32_t s_last_rgb = 0xFFFFFFFF;
static int s_dark_text = -1;  // text colour set for: 0 dark sky, 1 bright sky

static uint32_t sky_at(float p) {
  p = constrain(p, 0.0f, 1.0f);
  int k = 0;
  while (k < (int)(sizeof(kSky) / sizeof(kSky[0])) - 2 && p > kSky[k + 1].at) k++;
  const Stop &a = kSky[k], &b = kSky[k + 1];
  const float f = (p - a.at) / (b.at - a.at);
  uint32_t out = 0;
  for (int sh = 0; sh <= 16; sh += 8) {
    const float ca = (a.rgb >> sh) & 0xFF, cb = (b.rgb >> sh) & 0xFF;
    out |= (uint32_t)lroundf(ca + (cb - ca) * f) << sh;
  }
  return out;
}

static void on_tap(lv_event_t *) { sunrise_dismiss(); }

void ui_sunrise_init(lv_obj_t *scr) {
  s_panel = lv_obj_create(scr);
  lv_obj_remove_style_all(s_panel);
  lv_obj_set_size(s_panel, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_panel, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_panel, on_tap, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

  s_time = lv_label_create(s_panel);
  lv_obj_set_style_text_font(s_time, &font_clock, 0);
  lv_obj_align(s_time, LV_ALIGN_CENTER, 0, -20);
  s_note = lv_label_create(s_panel);
  lv_obj_set_style_text_font(s_note, &font_sm, 0);
  lv_obj_align(s_note, LV_ALIGN_BOTTOM_MID, 0, -32);
}

bool ui_sunrise_is_open() { return !lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN); }

static void set_text(lv_obj_t *l, const char *t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

void ui_sunrise_update() {
  const float p = sunrise_progress();
  if (p < 0) {
    if (ui_sunrise_is_open()) lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!ui_sunrise_is_open()) {
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);  // over the clock and any open screen
    s_last_rgb = 0xFFFFFFFF;
    s_dark_text = -1;
  }

  const uint32_t rgb = sky_at(p);
  // The colour only changes every few seconds in a real sunrise; don't redraw for nothing.
  if (rgb != s_last_rgb) {
    s_last_rgb = rgb;
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(rgb), 0);
    // Night amber on the dark sky, dark brown once it's bright.
    const int dark = p > 0.45f;
    if (dark != s_dark_text) {
      s_dark_text = dark;
      lv_obj_set_style_text_color(s_time, lv_color_hex(dark ? 0x4A2208 : 0xC2541B), 0);
      lv_obj_set_style_text_color(s_note, lv_color_hex(dark ? 0x6B3A14 : 0xA8461A), 0);
    }
  }

  char t[16] = "--:--";
  if (net_time_valid()) {
    const time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (g_settings.use_24h)
      snprintf(t, sizeof(t), "%02d:%02d", tm.tm_hour, tm.tm_min);
    else
      snprintf(t, sizeof(t), "%d:%02d", tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min);
  }
  set_text(s_time, t);

  char note[64];
  const int i = sunrise_alarm_index();
  if (i >= 0) {
    char hm[16];
    const Alarm &a = alarm_get(i);
    alarm_format_time(a.hour, a.minute, hm, sizeof(hm));
    snprintf(note, sizeof(note), "Sunrise for %s alarm  ·  tap to turn off", hm);
  } else {
    snprintf(note, sizeof(note), "Sunrise preview  ·  tap to end");
  }
  set_text(s_note, note);
}
