#include "ui_diag.h"

#include <Arduino.h>

#include "alarm.h"
#include "audio.h"
#include "board.h"
#include "diag.h"

LV_FONT_DECLARE(font_md);
LV_FONT_DECLARE(font_sm);

static const lv_color_t kBg = lv_color_hex(0x0A0C10);
static const lv_color_t kCardHi = lv_color_hex(0x1F242C);
static const lv_color_t kText = lv_color_hex(0xF2E9DC);
static const lv_color_t kMuted = lv_color_hex(0x7D8590);
static const lv_color_t kAccent = lv_color_hex(0xFFB547);

static lv_obj_t *s_root, *s_report, *s_touch, *s_dot;
static int s_brightness = -1;
static uint32_t s_last_report_ms = 0;
static char s_buf[768];

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, lv_event_cb_t cb, void *user = nullptr) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_style_bg_color(b, kCardHi, 0);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, 64);
  lv_obj_center(label(b, &font_sm, kText, text));
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

static void refresh_report() {
  diag_report(s_buf, sizeof(s_buf));
  lv_label_set_text(s_report, s_buf);
  s_last_report_ms = millis();
}

static void on_touch(lv_event_t *) {
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_obj_set_pos(s_dot, p.x - 14, p.y - 14);
  lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
  char t[48];
  snprintf(t, sizeof(t), "Touch: %d, %d", (int)p.x, (int)p.y);
  lv_label_set_text(s_touch, t);
}

static void on_close(lv_event_t *) { ui_diag_close(); }
static void on_sound(lv_event_t *) { audio_play(AlarmSound::Chimes, 60, false); }
static void on_stop(lv_event_t *) { audio_stop(); }
static void on_light(lv_event_t *e) { s_brightness = (int)(intptr_t)lv_event_get_user_data(e); }

void ui_diag_init(lv_obj_t *scr) {
  s_root = lv_obj_create(scr);
  lv_obj_remove_style_all(s_root);
  lv_obj_set_size(s_root, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_root, kBg, 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_root, on_touch, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(s_root, on_touch, LV_EVENT_PRESSED, nullptr);

  lv_obj_t *title = label(s_root, &font_md, kAccent, "Hardware check");
  lv_obj_set_pos(title, 24, 18);
  s_touch = label(s_root, &font_sm, kMuted, "Touch anywhere: a dot follows your finger");
  lv_obj_set_pos(s_touch, 300, 28);

  s_report = label(s_root, &font_sm, kText, "");
  lv_obj_set_pos(s_report, 24, 76);
  lv_obj_set_width(s_report, LCD_WIDTH - 48);
  lv_label_set_long_mode(s_report, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(s_report, 6, 0);

  // Bottom row: speaker test, backlight steps, close
  const int y = LCD_HEIGHT - 64 - 20;
  button(s_root, "Play sound", 24, y, 150, on_sound);
  button(s_root, "Stop", 184, y, 90, on_stop);
  button(s_root, "Light 5%", 300, y, 110, on_light, (void *)(intptr_t)5);
  button(s_root, "50%", 420, y, 80, on_light, (void *)(intptr_t)50);
  button(s_root, "100%", 510, y, 90, on_light, (void *)(intptr_t)100);
  button(s_root, "Close", LCD_WIDTH - 24 - 150, y, 150, on_close);

  s_dot = lv_obj_create(s_root);
  lv_obj_remove_style_all(s_dot);
  lv_obj_set_size(s_dot, 28, 28);
  lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(s_dot, kAccent, 0);
  lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

void ui_diag_open() {
  refresh_report();
  lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_root);
  diag_print();
}

void ui_diag_close() {
  audio_stop();
  s_brightness = -1;
  lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
}

bool ui_diag_is_open() { return s_root && !lv_obj_has_flag(s_root, LV_OBJ_FLAG_HIDDEN); }

int ui_diag_brightness() { return ui_diag_is_open() ? s_brightness : -1; }

void ui_diag_update() {
  if (!ui_diag_is_open()) return;
  // A ringing alarm takes over the screen.
  if (alarm_state() == AlarmState::Ringing) {
    ui_diag_close();
    return;
  }
  if (millis() - s_last_report_ms >= 2000) refresh_report();
}
