#include "ui_sleep.h"

#include <Arduino.h>

#include "board.h"
#include "display.h"
#include "settings.h"
#include "sleep.h"

LV_FONT_DECLARE(font_reading);
LV_FONT_DECLARE(font_md);
LV_FONT_DECLARE(font_sm);

static const lv_color_t kBg = lv_color_hex(0x0A0C10);
static const lv_color_t kCard = lv_color_hex(0x14171D);
static const lv_color_t kText = lv_color_hex(0xF2E9DC);
static const lv_color_t kMuted = lv_color_hex(0x7D8590);
static const lv_color_t kAccent = lv_color_hex(0xFFB547);
static const lv_color_t kOnAccent = lv_color_hex(0x1A1205);

static constexpr uint32_t kIdleCloseMs = 60000;
static constexpr int kTimerCount = sizeof(kSleepTimerChoices) / sizeof(kSleepTimerChoices[0]);

static lv_obj_t *s_panel, *s_sound_btn[(int)SleepSound::Count], *s_vol_value, *s_timer_btn[kTimerCount];
static lv_obj_t *s_go, *s_go_label;
static uint32_t s_last_activity_ms = 0;

static void show(lv_obj_t *o, bool v) {
  if (v)
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

// A button that lights up amber when chosen (LV_STATE_CHECKED).
static lv_obj_t *choice(lv_obj_t *parent, const char *text, const lv_font_t *font, int x, int y, int w, int h,
                        lv_event_cb_t cb, intptr_t value) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, kCard, 0);
  lv_obj_set_style_bg_color(b, kAccent, LV_STATE_CHECKED);
  lv_obj_t *l = label(b, font, kText, text);
  lv_obj_set_style_text_color(l, kOnAccent, LV_STATE_CHECKED);
  lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)value);
  return b;
}

static void set_checked(lv_obj_t *b, bool on) {
  if (on) {
    lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_add_state(lv_obj_get_child(b, 0), LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(b, LV_STATE_CHECKED);
    lv_obj_remove_state(lv_obj_get_child(b, 0), LV_STATE_CHECKED);
  }
}

static void refresh() {
  for (int i = 0; i < (int)SleepSound::Count; i++) set_checked(s_sound_btn[i], g_settings.sleep_sound == i);
  for (int i = 0; i < kTimerCount; i++) set_checked(s_timer_btn[i], g_settings.sleep_minutes == kSleepTimerChoices[i]);
  char b[48];
  snprintf(b, sizeof(b), "%d %%", g_settings.sleep_volume);
  lv_label_set_text(s_vol_value, b);
  if (sleep_active()) {
    const int left = sleep_minutes_left();
    if (left >= 0)
      snprintf(b, sizeof(b), "Stop  ·  %d min left", left);
    else
      snprintf(b, sizeof(b), "Stop");
    lv_obj_set_style_bg_color(s_go, kCard, 0);
    lv_obj_set_style_text_color(s_go_label, kText, 0);
  } else {
    snprintf(b, sizeof(b), "Play");
    lv_obj_set_style_bg_color(s_go, kAccent, 0);
    lv_obj_set_style_text_color(s_go_label, kOnAccent, 0);
  }
  if (strcmp(lv_label_get_text(s_go_label), b) != 0) lv_label_set_text(s_go_label, b);
}

static void on_sound(lv_event_t *e) {
  sleep_set_sound((SleepSound)(intptr_t)lv_event_get_user_data(e));
  if (!sleep_active()) sleep_start();  // picking a sound plays it
  refresh();
}

static void on_volume(lv_event_t *e) {
  const int step = (int)(intptr_t)lv_event_get_user_data(e);
  sleep_set_volume(constrain(g_settings.sleep_volume + step, 5, 100));
  refresh();
}

static void on_timer(lv_event_t *e) {
  sleep_set_timer(kSleepTimerChoices[(intptr_t)lv_event_get_user_data(e)]);
  refresh();
}

static void on_go(lv_event_t *) {
  if (sleep_active())
    sleep_stop();
  else
    sleep_start();
  refresh();
}

static void on_done(lv_event_t *) { ui_sleep_close(); }

void ui_sleep_init(lv_obj_t *scr) {
  s_panel = lv_obj_create(scr);
  lv_obj_remove_style_all(s_panel);
  lv_obj_set_size(s_panel, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_panel, kBg, 0);
  lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);  // swallow taps so they don't reach the clock
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
  show(s_panel, false);

  lv_obj_t *title = label(s_panel, &font_md, kText, "Sleep sounds");
  lv_obj_set_pos(title, 32, 24);
  lv_obj_t *done = lv_button_create(s_panel);
  lv_obj_set_size(done, 140, 56);
  lv_obj_align(done, LV_ALIGN_TOP_RIGHT, -32, 18);
  lv_obj_set_style_radius(done, 16, 0);
  lv_obj_set_style_shadow_width(done, 0, 0);
  lv_obj_set_style_bg_color(done, kCard, 0);
  lv_obj_t *dl = label(done, &font_sm, kText, "Done");
  lv_obj_center(dl);
  lv_obj_add_event_cb(done, on_done, LV_EVENT_CLICKED, nullptr);

  // Sound: two big tiles.
  const int w = (LCD_WIDTH - 64 - 16) / 2;
  for (int i = 0; i < (int)SleepSound::Count; i++)
    s_sound_btn[i] = choice(s_panel, sleep_sound_name((SleepSound)i), &font_md, 32 + i * (w + 16), 92, w, 110,
                            on_sound, i);

  // Volume: minus, value, plus.
  lv_obj_t *vl = label(s_panel, &font_sm, kMuted, "VOLUME");
  lv_obj_set_pos(vl, 32, 238);
  lv_obj_t *minus = choice(s_panel, LV_SYMBOL_MINUS, &lv_font_montserrat_24, 220, 222, 84, 56, on_volume, -5);
  s_vol_value = label(s_panel, &font_md, kText, "");
  lv_obj_set_width(s_vol_value, 140);
  lv_obj_set_style_text_align(s_vol_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(s_vol_value, 312, 234);
  lv_obj_t *plus = choice(s_panel, LV_SYMBOL_PLUS, &lv_font_montserrat_24, 460, 222, 84, 56, on_volume, 5);
  (void)minus;
  (void)plus;

  // Timer: a row of chips.
  lv_obj_t *tl = label(s_panel, &font_sm, kMuted, "STOP AFTER");
  lv_obj_set_pos(tl, 32, 306);
  const int cw = (LCD_WIDTH - 64 - 4 * 10) / kTimerCount;
  for (int i = 0; i < kTimerCount; i++)
    s_timer_btn[i] = choice(s_panel, sleep_timer_label(kSleepTimerChoices[i]), &font_sm, 32 + i * (cw + 10), 334,
                            cw, 56, on_timer, i);

  // Play / Stop
  s_go = lv_button_create(s_panel);
  lv_obj_set_size(s_go, LCD_WIDTH - 64, 64);
  lv_obj_set_pos(s_go, 32, 402);
  lv_obj_set_style_radius(s_go, 16, 0);
  lv_obj_set_style_shadow_width(s_go, 0, 0);
  s_go_label = label(s_go, &font_md, kOnAccent, "");
  lv_obj_center(s_go_label);
  lv_obj_add_event_cb(s_go, on_go, LV_EVENT_CLICKED, nullptr);
}

void ui_sleep_open() {
  s_last_activity_ms = millis();
  refresh();
  show(s_panel, true);
}

void ui_sleep_close() { show(s_panel, false); }

bool ui_sleep_is_open() { return !lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN); }

void ui_sleep_update() {
  if (!ui_sleep_is_open()) return;
  if (display_ms_since_touch() < millis() - s_last_activity_ms) s_last_activity_ms = millis() - display_ms_since_touch();
  if (millis() - s_last_activity_ms > kIdleCloseMs) {
    ui_sleep_close();
    return;
  }
  refresh();
}
