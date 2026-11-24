#include "ui_alarms.h"

#include <Arduino.h>
#include <time.h>

#include "alarm.h"
#include "audio.h"
#include "board.h"
#include "display.h"
#include "settings.h"
#include "sunrise.h"

LV_FONT_DECLARE(font_clock);
LV_FONT_DECLARE(font_reading);
LV_FONT_DECLARE(font_md);
LV_FONT_DECLARE(font_sm);

static const lv_color_t kBg = lv_color_hex(0x0A0C10);
static const lv_color_t kCard = lv_color_hex(0x14171D);
static const lv_color_t kCardHi = lv_color_hex(0x1F242C);
static const lv_color_t kText = lv_color_hex(0xF2E9DC);
static const lv_color_t kMuted = lv_color_hex(0x7D8590);
static const lv_color_t kAccent = lv_color_hex(0xFFB547);
static const lv_color_t kWarn = lv_color_hex(0xE07A5F);

static constexpr uint32_t kIdleCloseMs = 60000;  // list/editor close if left alone
static constexpr uint32_t kHoldToStopMs = 1200;

// Ringing
static lv_obj_t *s_ring, *s_ring_time, *s_ring_label, *s_ring_snooze, *s_ring_stop, *s_ring_stop_bar;
// List
static lv_obj_t *s_list, *s_list_rows, *s_list_add;
// Editor
static lv_obj_t *s_edit, *s_ed_title, *s_ed_hour, *s_ed_min, *s_ed_ampm, *s_ed_days[7], *s_ed_sound, *s_ed_volume,
    *s_ed_snooze, *s_ed_ramp, *s_ed_skip, *s_ed_delete, *s_ed_preview, *s_ed_sunrise;
static int s_ed_index = -1;
static bool s_ed_is_new = false;
static uint32_t s_last_activity_ms = 0;

static const uint8_t kSnoozeChoices[] = {5, 9, 10, 15, 20};
static const char *kDayLetters[] = {"M", "T", "W", "T", "F", "S", "S"};  // Monday first
static const int kDayBit[] = {1, 2, 3, 4, 5, 6, 0};                    // -> bit index (0 = Sunday)

// ---------- small builders ----------

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

static lv_obj_t *full_screen(lv_obj_t *scr) {
  lv_obj_t *o = lv_obj_create(scr);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(o, kBg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);  // swallow taps so they don't reach the clock
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  show(o, false);
  return o;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg, const lv_font_t *font,
                        lv_event_cb_t cb, void *user = nullptr) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *l = label(b, font, fg, text);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}

static void touch_activity(lv_event_t *) { s_last_activity_ms = millis(); }

// ---------- ringing screen ----------

static void on_snooze(lv_event_t *) { alarm_snooze(); }

static void on_stop_event(lv_event_t *e) {
  // Stop needs a deliberate hold so a sleepy swipe can't turn the alarm off.
  static uint32_t pressed_at = 0;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    pressed_at = millis();
  } else if (code == LV_EVENT_PRESSING) {
    const uint32_t held = millis() - pressed_at;
    lv_bar_set_value(s_ring_stop_bar, min<uint32_t>(100, held * 100 / kHoldToStopMs), LV_ANIM_OFF);
    if (held >= kHoldToStopMs) {
      lv_bar_set_value(s_ring_stop_bar, 0, LV_ANIM_OFF);
      alarm_stop();
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    lv_bar_set_value(s_ring_stop_bar, 0, LV_ANIM_OFF);
  }
}

static void build_ringing(lv_obj_t *scr) {
  s_ring = full_screen(scr);
  s_ring_time = label(s_ring, &font_reading, kText);
  lv_obj_align(s_ring_time, LV_ALIGN_TOP_MID, 0, 24);
  s_ring_label = label(s_ring, &font_sm, kMuted);
  lv_obj_align(s_ring_label, LV_ALIGN_TOP_MID, 0, 112);

  s_ring_snooze = button(s_ring, "SNOOZE", kAccent, lv_color_hex(0x1A1205), &font_reading, on_snooze);
  lv_obj_set_size(s_ring_snooze, LCD_WIDTH - 64, 200);
  lv_obj_align(s_ring_snooze, LV_ALIGN_TOP_MID, 0, 150);

  s_ring_stop = lv_obj_create(s_ring);
  lv_obj_remove_style_all(s_ring_stop);
  lv_obj_set_size(s_ring_stop, LCD_WIDTH - 64, 96);
  lv_obj_align(s_ring_stop, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_add_flag(s_ring_stop, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_ring_stop, LV_OBJ_FLAG_SCROLLABLE);
  s_ring_stop_bar = lv_bar_create(s_ring_stop);
  lv_obj_remove_style_all(s_ring_stop_bar);
  lv_obj_set_size(s_ring_stop_bar, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_ring_stop_bar, kCard, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_ring_stop_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_ring_stop_bar, 16, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_ring_stop_bar, kCardHi, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_ring_stop_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_ring_stop_bar, 16, LV_PART_INDICATOR);
  lv_obj_remove_flag(s_ring_stop_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_bar_set_range(s_ring_stop_bar, 0, 100);
  lv_obj_t *l = label(s_ring_stop, &font_md, kText, "Hold to stop");
  lv_obj_center(l);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
    lv_obj_add_event_cb(s_ring_stop, on_stop_event, c, nullptr);
}

static void update_ringing() {
  const bool ringing = alarm_state() == AlarmState::Ringing;
  show(s_ring, ringing);
  if (!ringing) return;
  lv_obj_move_foreground(s_ring);

  char buf[48];
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  alarm_format_time(t.tm_hour, t.tm_min, buf, sizeof(buf));
  lv_label_set_text(s_ring_time, buf);
  const Alarm &a = alarm_get(alarm_ringing_index());
  char hm[16];
  alarm_format_time(a.hour, a.minute, hm, sizeof(hm));
  snprintf(buf, sizeof(buf), "ALARM %s  ·  SNOOZE %d MIN", hm, a.snooze_min);
  lv_label_set_text(s_ring_label, buf);
}

// ---------- editor ----------

static void open_editor(int index, bool is_new);
static void rebuild_list();

static void build_hour_options() {
  static char opts[24 * 3 + 1];
  opts[0] = 0;
  for (int h = 0; h < (g_settings.use_24h ? 24 : 12); h++) {
    char b[5];
    snprintf(b, sizeof(b), g_settings.use_24h ? "%02d\n" : "%d\n", g_settings.use_24h ? h : (h == 0 ? 12 : h));
    strcat(opts, b);
  }
  opts[strlen(opts) - 1] = 0;
  lv_roller_set_options(s_ed_hour, opts, LV_ROLLER_MODE_INFINITE);
  show(s_ed_ampm, !g_settings.use_24h);
}

static Alarm editor_read() {
  Alarm a = alarm_get(s_ed_index);
  const int sel = lv_roller_get_selected(s_ed_hour);
  if (g_settings.use_24h)
    a.hour = sel;
  else
    a.hour = sel % 12 + (lv_roller_get_selected(s_ed_ampm) == 1 ? 12 : 0);
  a.minute = lv_roller_get_selected(s_ed_min);
  a.days = 0;
  for (int i = 0; i < 7; i++)
    if (lv_obj_has_state(s_ed_days[i], LV_STATE_CHECKED)) a.days |= 1 << kDayBit[i];
  a.sound = (AlarmSound)lv_dropdown_get_selected(s_ed_sound);
  a.volume = lv_slider_get_value(s_ed_volume);
  a.snooze_min = kSnoozeChoices[lv_dropdown_get_selected(s_ed_snooze)];
  a.ramp = lv_obj_has_state(s_ed_ramp, LV_STATE_CHECKED);
  a.skip_next = lv_obj_has_state(s_ed_skip, LV_STATE_CHECKED);
  a.sunrise_min = kSunriseChoices[lv_dropdown_get_selected(s_ed_sunrise)];
  a.enabled = true;
  return a;
}

static void stop_preview() {
  if (alarm_state() != AlarmState::Ringing) audio_stop();
  lv_label_set_text(lv_obj_get_child(s_ed_preview, 0), "Preview");
}

static void on_preview(lv_event_t *) {
  if (audio_playing() && alarm_state() != AlarmState::Ringing) {
    stop_preview();
    return;
  }
  const Alarm a = editor_read();
  audio_play(a.sound, a.volume, a.ramp, true);  // preview: the fade-up sped up and repeating
  lv_label_set_text(lv_obj_get_child(s_ed_preview, 0), "Stop");
}

// Try the sunrise now, sped up to about a minute. The editor stays open underneath.
static void on_sunrise_preview(lv_event_t *) {
  stop_preview();
  sunrise_preview_start();
}

static void on_editor_save(lv_event_t *) {
  stop_preview();
  alarm_set(s_ed_index, editor_read());
  show(s_edit, false);
  rebuild_list();
}

static void on_editor_cancel(lv_event_t *) {
  stop_preview();
  if (s_ed_is_new) alarm_remove(s_ed_index);
  show(s_edit, false);
  rebuild_list();
}

static void on_editor_delete(lv_event_t *) {
  stop_preview();
  alarm_remove(s_ed_index);
  show(s_edit, false);
  rebuild_list();
}

static lv_obj_t *make_roller(lv_obj_t *parent, int w) {
  lv_obj_t *r = lv_roller_create(parent);
  lv_obj_set_width(r, w);
  lv_roller_set_visible_row_count(r, 3);
  lv_obj_set_style_text_font(r, &font_md, 0);
  lv_obj_set_style_text_font(r, &font_md, LV_PART_SELECTED);
  lv_obj_set_style_bg_color(r, kCard, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_bg_color(r, kAccent, LV_PART_SELECTED);
  lv_obj_set_style_text_color(r, lv_color_hex(0x1A1205), LV_PART_SELECTED);
  return r;
}

// Dark styling for dropdowns, sliders and switches so they match the clock, whatever the theme.
static void style_dropdown(lv_obj_t *dd) {
  lv_obj_set_style_bg_color(dd, kCard, 0);
  lv_obj_set_style_text_color(dd, kText, 0);
  lv_obj_set_style_border_width(dd, 0, 0);
  lv_obj_set_style_radius(dd, 12, 0);
  lv_obj_set_style_text_font(dd, &font_sm, 0);
  lv_obj_set_style_text_font(dd, &lv_font_montserrat_20, LV_PART_INDICATOR);
  lv_obj_t *list = lv_dropdown_get_list(dd);
  lv_obj_set_style_bg_color(list, kCardHi, 0);
  lv_obj_set_style_text_color(list, kText, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_text_font(list, &font_sm, 0);
  lv_obj_set_style_bg_color(list, kAccent, (lv_style_selector_t)LV_PART_SELECTED | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(list, lv_color_hex(0x1A1205), (lv_style_selector_t)LV_PART_SELECTED | LV_STATE_CHECKED);
}

static void style_switch(lv_obj_t *sw) {
  lv_obj_set_style_bg_color(sw, kCardHi, 0);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(sw, kAccent, (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, kText, LV_PART_KNOB);
}

static lv_obj_t *row_label(lv_obj_t *parent, const char *text, int x, int y) {
  lv_obj_t *l = label(parent, &font_sm, kMuted, text);
  lv_obj_set_pos(l, x, y);
  return l;
}

// Dark letter on a lit (amber) day button, light letter otherwise.
static void sync_day(lv_obj_t *b) {
  const bool on = lv_obj_has_state(b, LV_STATE_CHECKED);
  lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? lv_color_hex(0x1A1205) : kText, 0);
}

static void on_day_toggled(lv_event_t *e) { sync_day((lv_obj_t *)lv_event_get_target(e)); }

static void build_editor(lv_obj_t *scr) {
  s_edit = full_screen(scr);
  lv_obj_add_event_cb(s_edit, touch_activity, LV_EVENT_PRESSED, nullptr);

  s_ed_title = label(s_edit, &font_sm, kMuted, "EDIT ALARM");
  lv_obj_set_style_text_letter_space(s_ed_title, 2, 0);
  lv_obj_set_pos(s_ed_title, 32, 22);

  // Time: rollers on the left.
  s_ed_hour = make_roller(s_edit, 100);
  lv_obj_set_pos(s_ed_hour, 32, 64);
  lv_obj_t *colon = label(s_edit, &font_md, kText, ":");
  lv_obj_set_pos(colon, 140, 110);
  s_ed_min = make_roller(s_edit, 100);
  lv_obj_set_pos(s_ed_min, 158, 64);
  static char mins[60 * 3 + 1];
  mins[0] = 0;
  for (int m = 0; m < 60; m++) {
    char b[4];
    snprintf(b, sizeof(b), "%02d\n", m);
    strcat(mins, b);
  }
  mins[strlen(mins) - 1] = 0;
  lv_roller_set_options(s_ed_min, mins, LV_ROLLER_MODE_INFINITE);
  s_ed_ampm = make_roller(s_edit, 90);
  lv_obj_set_pos(s_ed_ampm, 270, 64);
  lv_roller_set_options(s_ed_ampm, "AM\nPM", LV_ROLLER_MODE_NORMAL);

  // Days
  row_label(s_edit, "REPEAT (NONE = ONCE)", 32, 222);
  for (int i = 0; i < 7; i++) {
    lv_obj_t *b = lv_button_create(s_edit);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_size(b, 44, 44);
    lv_obj_set_pos(b, 32 + i * 50, 252);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, kCard, 0);
    lv_obj_set_style_bg_color(b, kAccent, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, kText, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0x1A1205), LV_STATE_CHECKED);
    lv_obj_t *l = label(b, &font_sm, kText, kDayLetters[i]);
    lv_obj_add_event_cb(b, on_day_toggled, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_center(l);
    s_ed_days[i] = b;
  }

  // Sunrise: how long before the alarm the screen starts to glow, and a quick preview.
  row_label(s_edit, "SUNRISE", 32, 322);
  s_ed_sunrise = lv_dropdown_create(s_edit);
  lv_obj_set_pos(s_ed_sunrise, 140, 310);
  lv_obj_set_width(s_ed_sunrise, 130);
  lv_dropdown_set_options(s_ed_sunrise, "Off\n15 min\n20 min\n30 min");
  style_dropdown(s_ed_sunrise);
  lv_obj_t *sun_try = button(s_edit, "Preview", kCard, kText, &font_sm, on_sunrise_preview);
  lv_obj_set_size(sun_try, 110, 44);
  lv_obj_set_pos(sun_try, 280, 310);

  // Options on the right.
  const int rx = 420;
  row_label(s_edit, "SOUND", rx, 64);
  s_ed_sound = lv_dropdown_create(s_edit);
  lv_obj_set_pos(s_ed_sound, rx, 92);
  lv_obj_set_width(s_ed_sound, 220);
  String opts;
  for (int i = 0; i < (int)AlarmSound::Count; i++) {
    if (i) opts += "\n";
    opts += audio_sound_name((AlarmSound)i);
  }
  lv_dropdown_set_options(s_ed_sound, opts.c_str());
  style_dropdown(s_ed_sound);
  s_ed_preview = button(s_edit, "Preview", kCard, kText, &font_sm, on_preview);
  lv_obj_set_size(s_ed_preview, 110, 44);
  lv_obj_set_pos(s_ed_preview, rx + 230, 92);

  row_label(s_edit, "VOLUME", rx, 150);
  s_ed_volume = lv_slider_create(s_edit);
  lv_slider_set_range(s_ed_volume, 10, 100);
  lv_obj_set_width(s_ed_volume, 320);
  lv_obj_set_pos(s_ed_volume, rx + 10, 186);
  lv_obj_set_style_bg_color(s_ed_volume, kCardHi, 0);
  lv_obj_set_style_bg_opa(s_ed_volume, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_ed_volume, kAccent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_ed_volume, kAccent, LV_PART_KNOB);

  row_label(s_edit, "SNOOZE", rx, 222);
  s_ed_snooze = lv_dropdown_create(s_edit);
  lv_obj_set_pos(s_ed_snooze, rx, 250);
  lv_obj_set_width(s_ed_snooze, 150);
  lv_dropdown_set_options(s_ed_snooze, "5 min\n9 min\n10 min\n15 min\n20 min");
  style_dropdown(s_ed_snooze);

  row_label(s_edit, "Start quiet, get louder", rx, 318);
  s_ed_ramp = lv_switch_create(s_edit);
  lv_obj_set_pos(s_ed_ramp, rx + 270, 312);
  style_switch(s_ed_ramp);

  row_label(s_edit, "Skip next time", rx, 364);
  s_ed_skip = lv_switch_create(s_edit);
  lv_obj_set_pos(s_ed_skip, rx + 270, 358);
  style_switch(s_ed_skip);

  // Bottom buttons
  lv_obj_t *cancel = button(s_edit, "Cancel", kCard, kText, &font_md, on_editor_cancel);
  lv_obj_set_size(cancel, 170, 64);
  lv_obj_set_pos(cancel, 32, 396);
  s_ed_delete = button(s_edit, "Delete", kCard, kWarn, &font_md, on_editor_delete);
  lv_obj_set_size(s_ed_delete, 170, 64);
  lv_obj_set_pos(s_ed_delete, 214, 396);
  lv_obj_t *save = button(s_edit, "Save", kAccent, lv_color_hex(0x1A1205), &font_md, on_editor_save);
  lv_obj_set_size(save, 336, 64);
  lv_obj_set_pos(save, rx + 12, 396);
}

static void open_editor(int index, bool is_new) {
  s_ed_index = index;
  s_ed_is_new = is_new;
  const Alarm &a = alarm_get(index);
  build_hour_options();
  if (g_settings.use_24h) {
    lv_roller_set_selected(s_ed_hour, a.hour, LV_ANIM_OFF);
  } else {
    lv_roller_set_selected(s_ed_hour, a.hour % 12, LV_ANIM_OFF);  // option 0 is "12"
    lv_roller_set_selected(s_ed_ampm, a.hour >= 12 ? 1 : 0, LV_ANIM_OFF);
  }
  lv_roller_set_selected(s_ed_min, a.minute, LV_ANIM_OFF);
  for (int i = 0; i < 7; i++) {
    if (a.days & (1 << kDayBit[i]))
      lv_obj_add_state(s_ed_days[i], LV_STATE_CHECKED);
    else
      lv_obj_remove_state(s_ed_days[i], LV_STATE_CHECKED);
    sync_day(s_ed_days[i]);
  }
  lv_dropdown_set_selected(s_ed_sound, (uint32_t)a.sound);
  lv_slider_set_value(s_ed_volume, a.volume, LV_ANIM_OFF);
  int snooze_sel = 1;
  for (int i = 0; i < (int)sizeof(kSnoozeChoices); i++)
    if (kSnoozeChoices[i] == a.snooze_min) snooze_sel = i;
  lv_dropdown_set_selected(s_ed_snooze, snooze_sel);
  if (a.ramp)
    lv_obj_add_state(s_ed_ramp, LV_STATE_CHECKED);
  else
    lv_obj_remove_state(s_ed_ramp, LV_STATE_CHECKED);
  if (a.skip_next)
    lv_obj_add_state(s_ed_skip, LV_STATE_CHECKED);
  else
    lv_obj_remove_state(s_ed_skip, LV_STATE_CHECKED);
  int sun_sel = 0;
  for (int i = 0; i < (int)sizeof(kSunriseChoices); i++)
    if (kSunriseChoices[i] == a.sunrise_min) sun_sel = i;
  lv_dropdown_set_selected(s_ed_sunrise, sun_sel);
  lv_label_set_text(s_ed_title, is_new ? "NEW ALARM" : "EDIT ALARM");
  lv_label_set_text(lv_obj_get_child(s_ed_preview, 0), "Preview");
  show(s_ed_delete, !is_new);
  s_last_activity_ms = millis();
  show(s_edit, true);
  lv_obj_move_foreground(s_edit);
}

// ---------- list ----------

static void on_row_click(lv_event_t *e) { open_editor((int)(intptr_t)lv_event_get_user_data(e), false); }

static void on_row_switch(lv_event_t *e) {
  lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  Alarm a = alarm_get(i);
  a.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
  if (!a.enabled) a.skip_next = false;
  alarm_set(i, a);
  rebuild_list();
}

static void on_add(lv_event_t *) {
  const int i = alarm_add();
  if (i >= 0) open_editor(i, true);
}

static void on_list_done(lv_event_t *) { show(s_list, false); }

static void rebuild_list() {
  lv_obj_clean(s_list_rows);
  char hm[16], days[40], sub[64];
  for (int i = 0; i < kMaxAlarms; i++) {
    const Alarm &a = alarm_get(i);
    if (!a.used) continue;
    lv_obj_t *row = lv_obj_create(s_list_rows);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 74);
    lv_obj_set_style_bg_color(row, kCard, 0);
    lv_obj_set_style_bg_color(row, kCardHi, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 16, 0);
    lv_obj_set_style_pad_hor(row, 20, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    alarm_format_time(a.hour, a.minute, hm, sizeof(hm));
    lv_obj_t *t = label(row, &font_md, a.enabled ? kText : kMuted, hm);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -13);
    alarm_format_days(a.days, days, sizeof(days));
    char sun[20] = "";
    if (a.sunrise_min) snprintf(sun, sizeof(sun), " · sunrise %d min", a.sunrise_min);
    snprintf(sub, sizeof(sub), "%s · %s%s%s", days, audio_sound_name(a.sound), sun,
             a.skip_next ? " · skipping next" : "");
    lv_obj_t *s = label(row, &font_sm, kMuted, sub);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 0, 19);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 72, 40);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    style_switch(sw);
    if (a.enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, on_row_switch, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
  }
  if (alarm_count_used() == 0) {
    lv_obj_t *l = label(s_list_rows, &font_md, kMuted, "No alarms yet.");
    lv_obj_set_style_pad_top(l, 40, 0);
  }
  show(s_list_add, alarm_count_used() < kMaxAlarms);
}

static void build_list(lv_obj_t *scr) {
  s_list = full_screen(scr);
  lv_obj_add_event_cb(s_list, touch_activity, LV_EVENT_PRESSED, nullptr);
  lv_obj_t *title = label(s_list, &font_sm, kMuted, "ALARMS");
  lv_obj_set_style_text_letter_space(title, 2, 0);
  lv_obj_set_pos(title, 32, 22);

  s_list_rows = lv_obj_create(s_list);
  lv_obj_remove_style_all(s_list_rows);
  lv_obj_set_pos(s_list_rows, 32, 58);
  lv_obj_set_size(s_list_rows, LCD_WIDTH - 64, 326);
  lv_obj_set_flex_flow(s_list_rows, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_list_rows, 10, 0);
  lv_obj_set_scroll_dir(s_list_rows, LV_DIR_VER);

  s_list_add = button(s_list, "+  Add alarm", kCard, kText, &font_md, on_add);
  lv_obj_set_size(s_list_add, 360, 64);
  lv_obj_set_pos(s_list_add, 32, 396);
  lv_obj_t *done = button(s_list, "Done", kAccent, lv_color_hex(0x1A1205), &font_md, on_list_done);
  lv_obj_set_size(done, 360, 64);
  lv_obj_set_pos(done, LCD_WIDTH - 32 - 360, 396);
}

// ---------- public ----------

void ui_alarms_init(lv_obj_t *scr) {
  build_list(scr);
  build_editor(scr);
  build_ringing(scr);
}

void ui_alarms_open_list() {
  rebuild_list();
  s_last_activity_ms = millis();
  show(s_list, true);
  lv_obj_move_foreground(s_list);
}

void ui_alarms_close_all() {
  if (!lv_obj_has_flag(s_edit, LV_OBJ_FLAG_HIDDEN)) {
    stop_preview();
    if (s_ed_is_new) alarm_remove(s_ed_index);  // a new alarm that was never saved
  }
  show(s_edit, false);
  show(s_list, false);
}

bool ui_alarms_open() {
  return !lv_obj_has_flag(s_list, LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_flag(s_edit, LV_OBJ_FLAG_HIDDEN) ||
         !lv_obj_has_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
}

void ui_alarms_update() {
  update_ringing();
  // Leave the list/editor without saving if nobody touches it for a while. Any touch counts,
  // including on buttons, rollers and switches (their presses never reach the screen's own
  // event handler, so the timer used to run out a minute after opening however busy you were).
  const bool editing = !lv_obj_has_flag(s_list, LV_OBJ_FLAG_HIDDEN) || !lv_obj_has_flag(s_edit, LV_OBJ_FLAG_HIDDEN);
  if (!editing) return;
  if (sunrise_previewing()) s_last_activity_ms = millis();  // watching the preview isn't leaving
  if (display_ms_since_touch() < millis() - s_last_activity_ms) s_last_activity_ms = millis() - display_ms_since_touch();
  if (millis() - s_last_activity_ms > kIdleCloseMs) ui_alarms_close_all();
}
