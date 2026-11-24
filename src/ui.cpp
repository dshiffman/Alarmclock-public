#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

#include "alarm.h"
#include "board.h"
#include "co2.h"
#include "net.h"
#include "sensor.h"
#include "settings.h"
#include "ui_alarms.h"
#include "ui_diag.h"
#include "ui_sleep.h"
#include "ui_sunrise.h"
#include "sleep.h"
#include "weather.h"

LV_FONT_DECLARE(font_clock);
LV_FONT_DECLARE(font_reading);
LV_FONT_DECLARE(font_value);
LV_FONT_DECLARE(font_sun);
LV_FONT_DECLARE(font_md);
LV_FONT_DECLARE(font_sm);

// ---- Palette: warm amber on near-black, easy on the eyes in a dark room ----
static const lv_color_t kBg = lv_color_hex(0x0A0C10);
static const lv_color_t kCard = lv_color_hex(0x14171D);
static const lv_color_t kText = lv_color_hex(0xF2E9DC);
static const lv_color_t kMuted = lv_color_hex(0x7D8590);
static const lv_color_t kAccent = lv_color_hex(0xFFB547);  // temperature
static const lv_color_t kCool = lv_color_hex(0x6FB7D9);    // humidity, water
static const lv_color_t kWarn = lv_color_hex(0xE07A5F);
static const lv_color_t kNightTime = lv_color_hex(0xC2541B);  // deep amber, kind to dark-adapted eyes
static const lv_color_t kDaylight = lv_color_hex(0x2A3446);   // tide chart: sunrise to sunset

// ---- Tide screen geometry ----
static constexpr uint32_t kTidePanelMs = 30000;  // tide screen closes itself after this
static constexpr int kTideDays = 5;                // today + 4
static constexpr int kTideStep = 30;               // minutes per chart point
static constexpr int kTidePoints = kTideDays * 24 * 60 / kTideStep + 1;
static constexpr int kChartX = 32, kChartY = 138, kChartW = LCD_WIDTH - 64, kChartH = 160;
static constexpr int kChartPad = 4;
static constexpr int kDayW = kChartW / kTideDays;
static constexpr int kPerDay = 5;                  // highs/lows listed per day (usually 4)
static constexpr int kMaxDots = 26;

// Main screen
static lv_obj_t *s_date;
static lv_obj_t *s_alarm_row, *s_alarm_icon, *s_alarm_text;
static lv_obj_t *s_sleep_row, *s_sleep_icon, *s_sleep_text;  // sleep sounds button / status
static lv_obj_t *s_time;
static lv_obj_t *s_ampm;
static lv_obj_t *s_notice;
static lv_obj_t *s_in_temp, *s_in_temp_sub, *s_in_hum, *s_in_hum_sub;
static lv_obj_t *s_out_temp, *s_out_sub, *s_out_hi, *s_out_lo, *s_out_hum;
static lv_obj_t *s_sunrise, *s_sunset;
static lv_obj_t *s_cards[3];
static bool s_night = false;

// Tide screen (double-tap)
static lv_obj_t *s_tide_panel;
static lv_obj_t *s_tide_state, *s_tide_next, *s_tide_place;
static lv_obj_t *s_tide_chart;
static lv_chart_series_t *s_tide_ser;
static lv_obj_t *s_tide_dot[kMaxDots];
static lv_obj_t *s_tide_now_dot;
static lv_obj_t *s_tide_daylight[kTideDays];  // lighter band from sunrise to sunset
static lv_obj_t *s_tide_day_line[kTideDays - 1];
static constexpr int kMaxFeetLines = 14;
static lv_obj_t *s_tide_ft_line[kMaxFeetLines];   // one per whole foot
static lv_obj_t *s_tide_ft_label[kMaxFeetLines];  // every other foot, in the left margin
static lv_obj_t *s_tide_day_name[kTideDays];
static lv_obj_t *s_tide_row[kTideDays][kPerDay];
static lv_obj_t *s_tide_empty;
static uint32_t s_tide_opened_ms = 0;
// Touch-to-read: drag a finger along the chart to read the predicted height at any time.
static lv_obj_t *s_tide_cursor, *s_tide_cursor_dot;
static bool s_tide_ok = false;             // chart drawn; the values below are current
static int64_t s_tide_start = 0, s_tide_end = 0;
static int32_t s_tide_ymin = 0, s_tide_ymax = 1;
static bool s_scrubbing = false;           // finger down on the chart
static bool s_scrub_moved = false;
static bool s_scrub_was = false;           // this press started on the chart
static lv_point_t s_scrub_p0;
static uint32_t s_scrub_t0 = 0;

// ---------- helpers ----------

// Only touch a label when its text actually changes, so LVGL doesn't redraw for nothing.
static void set_text(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (cur && strcmp(cur, text) == 0) return;
  lv_label_set_text(label, text);
}

static void show(lv_obj_t *o, bool visible) {
  if (visible)
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t *make_panel(lv_obj_t *parent) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);  // let taps reach the screen (double-tap)
  return c;
}

static lv_obj_t *make_dot(lv_obj_t *parent, int size, lv_color_t color) {
  lv_obj_t *d = make_panel(parent);
  lv_obj_set_size(d, size, size);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, color, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  return d;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h, const char *title) {
  lv_obj_t *c = make_panel(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, kCard, 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 18, 0);
  lv_obj_set_style_pad_all(c, 16, 0);

  lv_obj_t *t = make_label(c, &font_sm, kMuted);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_letter_space(t, 2, 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  return c;
}

static float to_display_temp(float c) { return g_settings.fahrenheit ? c * 9.0f / 5.0f + 32.0f : c; }

static void format_hm(int minutes, char *out, size_t n) {
  const int h = minutes / 60, m = minutes % 60;
  if (g_settings.use_24h) {
    snprintf(out, n, "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
  }
}

// Day: next alarm sits in the top right. Night: centered, small, under the time.
// The sleep sounds button sits just left of it by day; at night it only shows while a sound
// plays, small, under the alarm.
static void place_alarm_row() {
  if (s_night) {
    lv_obj_align(s_alarm_row, LV_ALIGN_TOP_MID, 0, 74 + 40 + 210);
    lv_obj_align(s_sleep_row, LV_ALIGN_TOP_MID, 0, 74 + 40 + 210 + 40);
  } else {
    lv_obj_align(s_alarm_row, LV_ALIGN_TOP_RIGHT, -32, 30);
    lv_obj_update_layout(s_alarm_row);
    lv_obj_align_to(s_sleep_row, s_alarm_row, LV_ALIGN_OUT_LEFT_MID, -36, 0);
  }
}

// Centers the digits, with AM/PM tucked at their top right, on the screen.
static void align_time_extras() {
  lv_obj_update_layout(lv_screen_active());
  const int32_t gap = 12;
  const int32_t extras = lv_obj_get_width(s_ampm);
  const int32_t total = lv_obj_get_width(s_time) + (extras > 0 ? gap + extras : 0);
  lv_obj_set_pos(s_time, (LCD_WIDTH - total) / 2, 70);
  lv_obj_align_to(s_ampm, s_time, LV_ALIGN_OUT_RIGHT_TOP, gap, 20);
  place_alarm_row();
}

// ---------- tide screen ----------

static void update_tides();

static void open_tides() {
  s_tide_opened_ms = millis();
  show(s_tide_panel, true);
  update_tides();
}

static void tide_cursor_hide();
static void close_tides() {
  s_scrubbing = false;
  if (s_tide_cursor) tide_cursor_hide();
  show(s_tide_panel, false);
}

// Double-tap opens the tides. LVGL's own double-click wants both taps within 10 px (about 1 mm
// here), which a finger rarely manages, so this counts any two quick taps close together.
static constexpr uint32_t kDoubleTapMs = 500;  // second tap within this of the first
static constexpr int32_t kDoubleTapPx = 60;    // ...and within about 6 mm of it
static void on_screen_tap(lv_event_t *e) {
  static uint32_t last_ms = 0;
  static lv_point_t last_pt = {0, 0};
  lv_point_t pt;
  lv_indev_get_point(lv_event_get_indev(e), &pt);
  const uint32_t now = millis();
  const int32_t dx = pt.x - last_pt.x, dy = pt.y - last_pt.y;
  if (last_ms && now - last_ms <= kDoubleTapMs && dx * dx + dy * dy <= kDoubleTapPx * kDoubleTapPx) {
    last_ms = 0;  // a third quick tap starts a new pair
    open_tides();
    return;
  }
  last_ms = now;
  last_pt = pt;
}

// Holding a finger on the clock for 3 s opens the hardware check screen.
static uint32_t s_press_start_ms = 0;
static bool s_hold_fired = false;
static void on_screen_hold(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    s_press_start_ms = lv_tick_get();
    s_hold_fired = false;
  } else if (!s_hold_fired && lv_tick_elaps(s_press_start_ms) >= 3000) {
    s_hold_fired = true;
    close_tides();
    ui_diag_open();
  }
}
static void tide_cursor_at(int32_t x);
static void tide_cursor_hide();
// A quick tap anywhere closes the tide screen, as before. Touching the chart (or the day lists
// under it) and dragging shows a cursor with the predicted time and height instead.
static void on_tide_panel_event(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_point_t pt;
  lv_indev_get_point(lv_event_get_indev(e), &pt);
  if (code == LV_EVENT_PRESSED) {
    s_scrub_p0 = pt;
    s_scrub_t0 = millis();
    s_scrub_moved = false;
    s_scrubbing = s_tide_ok && pt.y >= kChartY - 20;
    s_scrub_was = s_scrubbing;
    if (s_scrubbing) tide_cursor_at(pt.x);
  } else if (code == LV_EVENT_PRESSING) {
    const int32_t dx = pt.x - s_scrub_p0.x, dy = pt.y - s_scrub_p0.y;
    if (dx * dx + dy * dy > 12 * 12) s_scrub_moved = true;
    if (s_scrubbing) {
      tide_cursor_at(pt.x);
      s_tide_opened_ms = millis();  // don't time out while reading
    }
  } else if (code == LV_EVENT_RELEASED) {
    if (s_scrubbing) {
      s_scrubbing = false;
      tide_cursor_hide();
      update_tides();  // back to the "now" lines
    }
  } else if (code == LV_EVENT_CLICKED) {
    if (!s_scrub_moved && (!s_scrub_was || millis() - s_scrub_t0 < 400)) close_tides();
  }
}
static void on_sleep_row_click(lv_event_t *) {
  close_tides();
  ui_sleep_open();
}
static void on_alarm_row_click(lv_event_t *) {
  // While an alarm is snoozed the row reads "Snoozed, 7 min · Turn off": tapping it ends it.
  if (alarm_state() == AlarmState::Snoozed) {
    alarm_stop();
    return;
  }
  close_tides();
  ui_alarms_open_list();
}

static void build_tide_panel(lv_obj_t *scr) {
  s_tide_panel = lv_obj_create(scr);
  lv_obj_remove_style_all(s_tide_panel);
  lv_obj_set_size(s_tide_panel, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_style_bg_color(s_tide_panel, kBg, 0);
  lv_obj_set_style_bg_opa(s_tide_panel, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_tide_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_tide_panel, LV_OBJ_FLAG_CLICKABLE);
  for (lv_event_code_t c : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_CLICKED})
    lv_obj_add_event_cb(s_tide_panel, on_tide_panel_event, c, nullptr);

  lv_obj_t *title = make_label(s_tide_panel, &font_sm, kMuted);
  lv_label_set_text(title, "TIDES · NEXT 5 DAYS");
  lv_obj_set_style_text_letter_space(title, 2, 0);
  lv_obj_set_pos(title, 32, 22);
  s_tide_place = make_label(s_tide_panel, &font_sm, kMuted);
  lv_obj_align(s_tide_place, LV_ALIGN_TOP_RIGHT, -32, 22);

  s_tide_state = make_label(s_tide_panel, &font_md, kText);
  lv_obj_set_pos(s_tide_state, 32, 52);
  s_tide_next = make_label(s_tide_panel, &font_sm, kCool);
  lv_obj_set_pos(s_tide_next, 32, 96);

  // Daylight bands behind the curve: lighter from sunrise to sunset, the dark background at night.
  for (auto &b : s_tide_daylight) {
    b = make_panel(s_tide_panel);
    lv_obj_set_style_bg_color(b, kDaylight, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 6, 0);
    show(b, false);
  }

  // Whole-foot levels behind the curve.
  for (int i = 0; i < kMaxFeetLines; i++) {
    lv_obj_t *l = s_tide_ft_line[i] = make_panel(s_tide_panel);
    lv_obj_set_size(l, kChartW, 1);
    lv_obj_set_style_bg_color(l, kMuted, 0);
    show(l, false);
    lv_obj_t *t = s_tide_ft_label[i] = make_label(s_tide_panel, &font_sm, kMuted);
    lv_obj_set_width(t, kChartX - 6);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_RIGHT, 0);
    show(t, false);
  }

  // Day separators behind the curve.
  for (int i = 0; i < kTideDays - 1; i++) {
    s_tide_day_line[i] = make_panel(s_tide_panel);
    lv_obj_set_size(s_tide_day_line[i], 1, LCD_HEIGHT - kChartY - 16);
    lv_obj_set_pos(s_tide_day_line[i], kChartX + (i + 1) * kDayW, kChartY);
    lv_obj_set_style_bg_color(s_tide_day_line[i], kCard, 0);
    lv_obj_set_style_bg_opa(s_tide_day_line[i], LV_OPA_COVER, 0);
  }

  // Tide curve from midnight today through the end of the fifth day.
  s_tide_chart = lv_chart_create(s_tide_panel);
  lv_obj_remove_flag(s_tide_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_tide_chart, kChartX, kChartY);
  lv_obj_set_size(s_tide_chart, kChartW, kChartH);
  lv_chart_set_type(s_tide_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(s_tide_chart, kTidePoints);
  lv_chart_set_div_line_count(s_tide_chart, 0, 0);
  lv_obj_set_style_bg_opa(s_tide_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_tide_chart, 0, 0);
  lv_obj_set_style_pad_all(s_tide_chart, kChartPad, 0);
  lv_obj_set_style_line_width(s_tide_chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_width(s_tide_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(s_tide_chart, 0, LV_PART_INDICATOR);
  s_tide_ser = lv_chart_add_series(s_tide_chart, kCool, LV_CHART_AXIS_PRIMARY_Y);

  for (auto &d : s_tide_dot) d = make_dot(s_tide_panel, 8, kCool);
  s_tide_now_dot = make_dot(s_tide_panel, 14, kAccent);

  s_tide_cursor = make_panel(s_tide_panel);
  lv_obj_set_size(s_tide_cursor, 2, kChartH);
  lv_obj_set_style_bg_color(s_tide_cursor, kText, 0);
  lv_obj_set_style_bg_opa(s_tide_cursor, LV_OPA_60, 0);
  s_tide_cursor_dot = make_dot(s_tide_panel, 16, kText);
  show(s_tide_cursor, false);
  show(s_tide_cursor_dot, false);

  // Under each day: its name, then its highs and lows.
  for (int d = 0; d < kTideDays; d++) {
    const int x = kChartX + d * kDayW + 10;
    s_tide_day_name[d] = make_label(s_tide_panel, &font_sm, kText);
    lv_obj_set_style_text_letter_space(s_tide_day_name[d], 1, 0);
    lv_obj_set_pos(s_tide_day_name[d], x, kChartY + kChartH + 8);
    for (int r = 0; r < kPerDay; r++) {
      s_tide_row[d][r] = make_label(s_tide_panel, &font_sm, kText);
      lv_obj_set_pos(s_tide_row[d][r], x, kChartY + kChartH + 38 + r * 25);
    }
  }

  s_tide_empty = make_label(s_tide_panel, &font_md, kMuted);
  lv_obj_set_width(s_tide_empty, LCD_WIDTH - 64);
  lv_obj_set_style_text_align(s_tide_empty, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_tide_empty, LV_ALIGN_CENTER, 0, 0);

  show(s_tide_panel, false);
}

// Tide height between two predicted highs/lows follows roughly half a cosine wave.
static float tide_height_at(const Tides &t, int64_t when) {
  for (int i = 0; i + 1 < t.count; i++) {
    const TideEvent &a = t.events[i], &b = t.events[i + 1];
    if (when >= a.when && when <= b.when) {
      const float f = (float)(when - a.when) / (float)max<int64_t>(1, b.when - a.when);
      return a.height_ft + (b.height_ft - a.height_ft) * (1 - cosf(PI * f)) / 2;
    }
  }
  return NAN;
}

static void format_duration(int64_t minutes, char *out, size_t n) {
  if (minutes < 60)
    snprintf(out, n, "%d min", (int)minutes);
  else
    snprintf(out, n, "%d h %d min", (int)(minutes / 60), (int)(minutes % 60));
}

// Compact time for the day lists: "9:32a" / "11:37p", or "09:32" in 24-hour mode.
static void format_hm_short(int minutes, char *out, size_t n) {
  const int h = minutes / 60, m = minutes % 60;
  if (g_settings.use_24h)
    snprintf(out, n, "%02d:%02d", h, m);
  else
    snprintf(out, n, "%d:%02d%c", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? 'a' : 'p');
}

static void update_tides() {
  settings_lock();
  const String place = g_settings.place;
  const bool have_station = g_settings.tide_station.length() > 0;
  settings_unlock();
  set_text(s_tide_place, place.c_str());

  const Tides t = weather_tides();
  const bool ok = t.valid;
  s_tide_ok = false;
  for (lv_obj_t *o : {s_tide_chart, s_tide_state, s_tide_next, s_tide_now_dot}) show(o, ok);
  for (auto &l : s_tide_day_line) show(l, ok);
  for (auto &l : s_tide_day_name) show(l, ok);
  for (auto &day : s_tide_row)
    for (auto &l : day) show(l, ok);
  for (auto &d : s_tide_dot) show(d, false);
  if (!ok) {
    for (auto &b : s_tide_daylight) show(b, false);
    for (auto &l : s_tide_ft_line) show(l, false);
    for (auto &l : s_tide_ft_label) show(l, false);
  }
  show(s_tide_empty, !ok);
  if (!ok) {
    set_text(s_tide_empty, !have_station                         ? "No tide station set.\nAdd one on the setup page."
                           : net_state() == NetState::Online ? "Loading tides..."
                                                              : "Tides need WiFi.");
    return;
  }

  const int64_t now = weather_now_local_min();
  const int64_t start = now - now % 1440;  // midnight today
  const int64_t end = start + kTideDays * 1440;
  int next = 0;
  while (next < t.count && t.events[next].when <= now) next++;
  char buf[64], dur[24];

  const float h_now = tide_height_at(t, now);
  if (next < t.count) {
    const TideEvent &n = t.events[next];
    if (isnan(h_now))
      snprintf(buf, sizeof(buf), "%s", n.high ? "Rising" : "Falling");
    else
      snprintf(buf, sizeof(buf), "%s · %.1f ft now", n.high ? "Rising" : "Falling", h_now);
    set_text(s_tide_state, buf);
    format_duration(n.when - now, dur, sizeof(dur));
    snprintf(buf, sizeof(buf), "%s tide at %s, in %s", n.high ? "High" : "Low", n.label, dur);
    set_text(s_tide_next, buf);
  }

  // Curve, in hundredths of a foot.
  float lo = 1e9, hi = -1e9;
  for (int i = 0; i < kTidePoints; i++) {
    const float h = tide_height_at(t, start + (int64_t)i * kTideStep);
    if (isnan(h)) {
      lv_chart_set_series_value_by_id(s_tide_chart, s_tide_ser, i, LV_CHART_POINT_NONE);
      continue;
    }
    lo = min(lo, h);
    hi = max(hi, h);
    lv_chart_set_series_value_by_id(s_tide_chart, s_tide_ser, i, lroundf(h * 100));
  }
  if (lo > hi) lo = hi = 0;
  const int32_t y_min = lroundf(lo * 100) - 30, y_max = lroundf(hi * 100) + 30;
  lv_chart_set_axis_range(s_tide_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
  lv_chart_refresh(s_tide_chart);
  s_tide_ok = true;
  s_tide_start = start;
  s_tide_end = end;
  s_tide_ymin = y_min;
  s_tide_ymax = y_max;

  const int inner_w = kChartW - 2 * kChartPad, inner_h = kChartH - 2 * kChartPad;
  auto px = [&](int64_t when) { return kChartX + kChartPad + (int)((when - start) * inner_w / (end - start)); };
  auto py = [&](float h) {
    return kChartY + kChartPad + inner_h - (int)((lroundf(h * 100) - y_min) * inner_h / max<int32_t>(1, y_max - y_min));
  };
  if (!isnan(h_now)) lv_obj_set_pos(s_tide_now_dot, px(now) - 7, py(h_now) - 7);

  // Whole-foot lines across the chart's range; 0 ft a little stronger. Labels every 2 ft.
  {
    const int ft_lo = (int)ceilf(y_min / 100.0f), ft_hi = (int)floorf(y_max / 100.0f);
    const int label_h = lv_font_get_line_height(&font_sm);
    int i = 0;
    for (int ft = ft_lo; ft <= ft_hi && i < kMaxFeetLines; ft++, i++) {
      const int y = py(ft);
      lv_obj_t *l = s_tide_ft_line[i];
      lv_obj_set_pos(l, kChartX, y);
      lv_obj_set_style_bg_opa(l, ft == 0 ? LV_OPA_60 : LV_OPA_30, 0);
      show(l, true);
      lv_obj_t *t = s_tide_ft_label[i];
      snprintf(buf, sizeof(buf), "%d", ft);
      set_text(t, buf);
      lv_obj_set_pos(t, 0, y - label_h / 2);
      show(t, ft % 2 == 0);
    }
    for (; i < kMaxFeetLines; i++) {
      show(s_tide_ft_line[i], false);
      show(s_tide_ft_label[i], false);
    }
  }

  // Daylight shading. Days without their own sun times borrow today's (they shift only a
  // minute or two a day).
  const Outdoor o = weather_outdoor();
  for (int d = 0; d < kTideDays; d++) {
    int rise = d < Outdoor::kSunDays ? o.day_sunrise_min[d] : -1;
    int set = d < Outdoor::kSunDays ? o.day_sunset_min[d] : -1;
    if (rise < 0 || set <= rise) rise = o.sunrise_min, set = o.sunset_min;
    lv_obj_t *b = s_tide_daylight[d];
    if (rise < 0 || set <= rise) {
      show(b, false);
      continue;
    }
    const int x0 = px(start + d * 1440 + rise), x1 = px(start + d * 1440 + set);
    lv_obj_set_pos(b, x0, kChartY);
    lv_obj_set_size(b, max(1, x1 - x0), kChartH);
    show(b, true);
  }

  // Day names and per-day lists.
  int rows[kTideDays] = {0};
  for (int d = 0; d < kTideDays; d++) {
    if (d == 0) {
      set_text(s_tide_day_name[d], "TODAY");
    } else {
      time_t at = time(nullptr) + d * 86400;
      struct tm lt;
      localtime_r(&at, &lt);
      strftime(buf, sizeof(buf), "%a %e", &lt);
      for (char *c = buf; *c; c++) *c = toupper(*c);
      char *dbl = strstr(buf, "  ");
      if (dbl) memmove(dbl, dbl + 1, strlen(dbl));
      set_text(s_tide_day_name[d], buf);
    }
  }
  int dots = 0;
  for (int k = 0; k < t.count; k++) {
    const TideEvent &e = t.events[k];
    if (e.when < start || e.when >= end) continue;
    if (dots < kMaxDots) {
      lv_obj_set_pos(s_tide_dot[dots], px(e.when) - 4, py(e.height_ft) - 4);
      show(s_tide_dot[dots], true);
      dots++;
    }
    const int d = (int)((e.when - start) / 1440);
    if (rows[d] >= kPerDay) continue;
    char hm[16];
    format_hm_short(e.hm, hm, sizeof(hm));
    snprintf(buf, sizeof(buf), "%s %s %.1f", e.high ? "H" : "L", hm, e.height_ft);
    lv_obj_t *l = s_tide_row[d][rows[d]++];
    set_text(l, buf);
    // Highs bright, lows muted; tides already past today are dimmed.
    lv_obj_set_style_text_color(l, e.high ? kText : kCool, 0);
    lv_obj_set_style_text_opa(l, e.when < now ? LV_OPA_40 : LV_OPA_COVER, 0);
  }
  for (int d = 0; d < kTideDays; d++)
    for (int r = rows[d]; r < kPerDay; r++) set_text(s_tide_row[d][r], "");
}

static void tide_cursor_hide() {
  show(s_tide_cursor, false);
  show(s_tide_cursor_dot, false);
}

// Cursor at screen x: a line down the chart, a dot on the curve, and the time and predicted
// height in the two lines above the chart (they go back to "now" when the finger lifts).
static void tide_cursor_at(int32_t x) {
  if (!s_tide_ok) return;
  const int inner_w = kChartW - 2 * kChartPad, inner_h = kChartH - 2 * kChartPad;
  const int x0 = kChartX + kChartPad;
  x = max<int32_t>(x0, min<int32_t>(x0 + inner_w, x));
  int64_t when = s_tide_start + (int64_t)(x - x0) * (s_tide_end - s_tide_start) / inner_w;
  when = (when + 2) / 5 * 5;  // nearest 5 minutes (one pixel is about 10)
  if (when >= s_tide_end) when = s_tide_end - 5;

  const Tides t = weather_tides();
  const float h = tide_height_at(t, when);
  lv_obj_set_pos(s_tide_cursor, x - 1, kChartY);
  show(s_tide_cursor, true);

  // "Today" or the weekday, then the time
  const int d = (int)((when - s_tide_start) / 1440);
  char day[12];
  if (d == 0) {
    snprintf(day, sizeof(day), "Today");
  } else {
    time_t at = time(nullptr) + d * 86400;
    struct tm lt;
    localtime_r(&at, &lt);
    strftime(day, sizeof(day), "%a", &lt);
  }
  const int m = (int)(when % 1440), hh = m / 60, mm = m % 60;
  char hm[16];
  if (g_settings.use_24h)
    snprintf(hm, sizeof(hm), "%02d:%02d", hh, mm);
  else
    snprintf(hm, sizeof(hm), "%d:%02d %s", hh % 12 == 0 ? 12 : hh % 12, mm, hh < 12 ? "AM" : "PM");

  char buf[64];
  if (isnan(h)) {
    snprintf(buf, sizeof(buf), "%s %s · no prediction", day, hm);
    set_text(s_tide_state, buf);
    set_text(s_tide_next, "");
    show(s_tide_cursor_dot, false);
    return;
  }
  snprintf(buf, sizeof(buf), "%s %s · %.1f ft", day, hm, h);
  set_text(s_tide_state, buf);
  const int py = kChartY + kChartPad + inner_h -
                 (int)((lroundf(h * 100) - s_tide_ymin) * inner_h / max<int32_t>(1, s_tide_ymax - s_tide_ymin));
  lv_obj_set_pos(s_tide_cursor_dot, x - 8, py - 8);
  show(s_tide_cursor_dot, true);

  // Which way it's going: the next high or low after this moment
  int k = 0;
  while (k < t.count && t.events[k].when <= when) k++;
  if (k < t.count) {
    const TideEvent &n = t.events[k];
    char nhm[16];
    format_hm_short(n.hm, nhm, sizeof(nhm));
    snprintf(buf, sizeof(buf), "%s · %s %.1f ft at %s", n.high ? "Rising" : "Falling", n.high ? "high" : "low",
             n.height_ft, nhm);
    set_text(s_tide_next, buf);
  } else {
    set_text(s_tide_next, "");
  }
}

// ---------- main screen ----------

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, kBg, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, on_screen_tap, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_event_cb(scr, on_screen_hold, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(scr, on_screen_hold, LV_EVENT_LONG_PRESSED_REPEAT, nullptr);

  // Top row: date on the left, next alarm on the right.
  s_date = make_label(scr, &font_md, kMuted);
  lv_obj_align(s_date, LV_ALIGN_TOP_LEFT, 32, 22);

  s_alarm_row = make_panel(scr);
  lv_obj_set_size(s_alarm_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_alarm_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_alarm_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_alarm_row, 8, 0);
  s_alarm_icon = make_label(s_alarm_row, &lv_font_montserrat_20, kMuted);
  lv_label_set_text(s_alarm_icon, LV_SYMBOL_BELL);
  s_alarm_text = make_label(s_alarm_row, &font_sm, kMuted);
  lv_obj_align(s_alarm_row, LV_ALIGN_TOP_RIGHT, -32, 30);
  // Tap the alarm status to see and edit alarms. Give it a generous touch target.
  lv_obj_add_flag(s_alarm_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(s_alarm_row, 24);
  lv_obj_add_event_cb(s_alarm_row, on_alarm_row_click, LV_EVENT_CLICKED, nullptr);

  // Sleep sounds: a speaker icon; while one plays, also the minutes left.
  s_sleep_row = make_panel(scr);
  lv_obj_set_size(s_sleep_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_sleep_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_sleep_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_sleep_row, 8, 0);
  s_sleep_icon = make_label(s_sleep_row, &lv_font_montserrat_20, kMuted);
  lv_label_set_text(s_sleep_icon, LV_SYMBOL_AUDIO);
  s_sleep_text = make_label(s_sleep_row, &font_sm, kCool);
  lv_obj_add_flag(s_sleep_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(s_sleep_row, 24);
  lv_obj_add_event_cb(s_sleep_row, on_sleep_row_click, LV_EVENT_CLICKED, nullptr);

  // Big clock, centered, with AM/PM at its top right.
  s_time = make_label(scr, &font_clock, kText);
  lv_label_set_text(s_time, "--:--");
  s_ampm = make_label(scr, &font_md, kMuted);
  align_time_extras();

  // Message under the clock while setting up or offline (hidden otherwise).
  s_notice = make_label(scr, &font_sm, kAccent);
  lv_obj_set_width(s_notice, 720);
  lv_obj_set_style_text_align(s_notice, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_notice, LV_ALIGN_TOP_MID, 0, 284);  // between the clock (ends 276) and the cards (316)
  show(s_notice, false);

  // Bottom cards: inside, outside, sun. Positions inside a card are from its padded content area
  // (card + 16); kReadingY puts the readings' baseline 80 px down.
  const int y = 316, h = 144, gap = 16, x0 = 24;
  const int w_out = 228, w_sun = 180, w_in = LCD_WIDTH - 2 * x0 - 2 * gap - w_out - w_sun;  // 312
  const int kReadingY = 40;

  lv_obj_t *c_in = s_cards[0] = make_card(scr, x0, y, w_in, h, "INSIDE");
  s_in_temp = make_label(c_in, &font_value, kAccent);
  lv_label_set_text(s_in_temp, "--");
  lv_obj_set_pos(s_in_temp, -3, kReadingY);
  s_in_temp_sub = make_label(c_in, &font_sm, kMuted);
  lv_obj_align(s_in_temp_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  s_in_hum = make_label(c_in, &font_value, kCool);
  lv_label_set_text(s_in_hum, "--");
  lv_obj_set_pos(s_in_hum, 147, kReadingY);
  s_in_hum_sub = make_label(c_in, &font_sm, kMuted);
  lv_obj_align(s_in_hum_sub, LV_ALIGN_BOTTOM_LEFT, 150, 0);

  // OUTSIDE: temperature, today's high and low stacked beside it, then "Cloudy  90%".
  lv_obj_t *c_out = s_cards[1] = make_card(scr, x0 + w_in + gap, y, w_out, h, "OUTSIDE");
  s_out_temp = make_label(c_out, &font_value, kText);
  lv_label_set_text(s_out_temp, "--");
  lv_obj_set_pos(s_out_temp, -3, kReadingY);
  s_out_hi = make_label(c_out, &font_sm, kMuted);
  s_out_lo = make_label(c_out, &font_sm, kMuted);
  s_out_sub = make_label(c_out, &font_sm, kMuted);
  lv_obj_align(s_out_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  s_out_hum = make_label(c_out, &font_sm, kCool);

  lv_obj_t *c_sun = s_cards[2] = make_card(scr, x0 + w_in + w_out + 2 * gap, y, w_sun, h, "SUNRISE");
  s_sunrise = make_label(c_sun, &font_sun, kText);
  lv_label_set_text(s_sunrise, "--");
  lv_obj_align(s_sunrise, LV_ALIGN_TOP_LEFT, 0, 28);
  lv_obj_t *set_title = make_label(c_sun, &font_sm, kMuted);
  lv_label_set_text(set_title, "SUNSET");
  lv_obj_set_style_text_letter_space(set_title, 2, 0);
  lv_obj_align(set_title, LV_ALIGN_TOP_LEFT, 0, 61);
  s_sunset = make_label(c_sun, &font_sun, kText);
  lv_label_set_text(s_sunset, "--");
  lv_obj_align(s_sunset, LV_ALIGN_TOP_LEFT, 0, 90);

  build_tide_panel(scr);
  ui_alarms_init(scr);
  ui_sleep_init(scr);
  ui_sunrise_init(scr);
  ui_diag_init(scr);
}

void ui_set_night(bool night) {
  if (night == s_night) return;
  s_night = night;
  if (night) {
    close_tides();
    ui_alarms_close_all();
    ui_sleep_close();
  }
  lv_obj_set_style_text_color(s_time, night ? kNightTime : kText, 0);
  lv_obj_set_style_text_color(s_ampm, night ? kNightTime : kMuted, 0);
  lv_obj_set_style_text_color(s_alarm_icon, night ? kNightTime : kMuted, 0);
  lv_obj_set_style_text_color(s_alarm_text, night ? kNightTime : kMuted, 0);
  lv_obj_set_style_text_color(s_sleep_icon, night ? kNightTime : kMuted, 0);
  lv_obj_set_style_text_color(s_sleep_text, night ? kNightTime : kCool, 0);
  // Pure black at night: the panel's backlight already glows a little on its own.
  lv_obj_set_style_bg_color(lv_screen_active(), night ? lv_color_black() : kBg, 0);
  for (lv_obj_t *o : {s_cards[0], s_cards[1], s_cards[2], s_date}) show(o, !night);
  // Drop the time toward the middle of the screen when it's on its own.
  lv_obj_set_style_translate_y(s_time, night ? 40 : 0, 0);
  lv_obj_set_style_translate_y(s_ampm, night ? 40 : 0, 0);
  place_alarm_row();
}

static void update_clock() {
  char buf[48];
  if (!net_time_valid()) {
    const bool changed = strcmp(lv_label_get_text(s_time), "--:--") != 0;
    set_text(s_time, "--:--");
    set_text(s_ampm, "");
    set_text(s_date, "");
    if (changed) align_time_extras();
    return;
  }

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  if (g_settings.use_24h) {
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    set_text(s_ampm, "");
  } else {
    int h12 = t.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, sizeof(buf), "%d:%02d", h12, t.tm_min);
    set_text(s_ampm, t.tm_hour < 12 ? "AM" : "PM");
  }
  const bool changed = strcmp(lv_label_get_text(s_time), buf) != 0;
  set_text(s_time, buf);
  if (changed) align_time_extras();

  strftime(buf, sizeof(buf), "%A, %B %e", &t);
  // %e pads single-digit days with a space; collapse the double space.
  char *dbl = strstr(buf, "  ");
  if (dbl) memmove(dbl, dbl + 1, strlen(dbl));
  set_text(s_date, buf);
}

static void update_status() {
  char buf[96];
  const char *msg = nullptr;
  switch (net_state()) {
    case NetState::SetupPortal:
      snprintf(buf, sizeof(buf), "To set up, join the WiFi network \"%s\" on your phone.", kSetupApName);
      msg = buf;
      break;
    case NetState::Connecting:
      msg = net_time_valid() ? "WiFi disconnected, reconnecting..." : "Connecting to WiFi...";
      break;
    case NetState::Online:
      if (!net_time_valid()) msg = "Getting the time...";
      break;
  }
  // Stuffy air: only said when it matters, and only if nothing more urgent is showing. The
  // colour is only set when it changes: setting a style makes LVGL redraw the label.
  static int notice_warn = -1;
  if (notice_warn != (msg ? 0 : 1)) {
    notice_warn = msg ? 0 : 1;
    lv_obj_set_style_text_color(s_notice, msg ? kAccent : kWarn, 0);
  }
  if (!msg && co2_high()) {
    const int ppm = co2_ppm();
    snprintf(buf, sizeof(buf), "CO2 %d,%03d ppm · open a window", ppm / 1000, ppm % 1000);
    msg = buf;
  }
  if (msg) set_text(s_notice, msg);
  show(s_notice, msg != nullptr && !s_night);

  const size_t old_alarm_len = strlen(lv_label_get_text(s_alarm_text));
  char alarm[48];
  const bool has_alarm = alarm_next_label(alarm, sizeof(alarm));
  if (alarm_state() == AlarmState::Snoozed) strlcat(alarm, "  ·  Turn off", sizeof(alarm));
  set_text(s_alarm_text, has_alarm ? alarm : "No alarm set");

  char sl[16] = "";
  if (sleep_active()) {
    const int left = sleep_minutes_left();
    if (left >= 0) snprintf(sl, sizeof(sl), "%d min", left);
  }
  const size_t old_len = strlen(lv_label_get_text(s_sleep_text));
  set_text(s_sleep_text, sl);
  show(s_sleep_text, sl[0] != 0);
  // Styles and positions only change on a change, so LVGL doesn't redraw for nothing.
  static int icon_lit = -1;
  const int lit = sleep_active() && !s_night;
  if (lit != icon_lit && !s_night) {
    icon_lit = lit;
    lv_obj_set_style_text_color(s_sleep_icon, lit ? kCool : kMuted, 0);
  }
  if (s_night) icon_lit = -1;  // ui_set_night() set the night colour
  show(s_sleep_row, !s_night || sleep_active());
  if (old_len != strlen(sl) || old_alarm_len != strlen(lv_label_get_text(s_alarm_text)))
    place_alarm_row();  // a width changed

}

static void update_inside() {
  char buf[32];
  // Setting a style makes LVGL redraw the label, so the colour is only set when it changes.
  static int sub_warn = -1;
  auto set_sub_warn = [](bool warn) {
    if (sub_warn == (int)warn) return;
    sub_warn = warn;
    lv_obj_set_style_text_color(s_in_temp_sub, warn ? kWarn : kMuted, 0);
  };
  if (!sensor_present()) {
    set_text(s_in_temp, "--");
    set_text(s_in_hum, "--");
    set_text(s_in_temp_sub, "Sensor not found");
    set_sub_warn(true);
    set_text(s_in_hum_sub, "");
    return;
  }
  IndoorReading r = sensor_latest();
  if (!r.valid) return;

  set_sub_warn(false);
  snprintf(buf, sizeof(buf), "%d°", (int)lroundf(to_display_temp(r.temp_c)));
  set_text(s_in_temp, buf);
  set_text(s_in_temp_sub, "Temperature");

  snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(r.humidity));
  set_text(s_in_hum, buf);
  set_text(s_in_hum_sub, r.humidity < 30 ? "Humidity · dry" : r.humidity > 60 ? "Humidity · damp" : "Humidity");
}

static void update_outside() {
  char buf[40];
  const Outdoor o = weather_outdoor();
  if (!o.valid) {
    set_text(s_out_temp, "--");
    set_text(s_out_hi, "");
    set_text(s_out_lo, "");
    set_text(s_out_hum, "");
    settings_lock();
    const bool no_place = isnan(g_settings.lat);
    settings_unlock();
    set_text(s_out_sub, no_place ? "Set location" : "");
    set_text(s_sunrise, "--");
    set_text(s_sunset, "--");
    return;
  }
  snprintf(buf, sizeof(buf), "%d°", (int)lroundf(to_display_temp(o.temp_c)));
  set_text(s_out_temp, buf);
  snprintf(buf, sizeof(buf), "H %d°", (int)lroundf(to_display_temp(o.high_c)));
  set_text(s_out_hi, buf);
  snprintf(buf, sizeof(buf), "L %d°", (int)lroundf(to_display_temp(o.low_c)));
  set_text(s_out_lo, buf);
  const char *cond = o.condition;
  set_text(s_out_sub, cond);
  if (isnan(o.humidity)) {
    set_text(s_out_hum, "");
  } else {
    snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(o.humidity));
    set_text(s_out_hum, buf);
  }
  // High/low sit beside the temperature (H level with the digit tops, L on their baseline),
  // humidity after the condition. Both depend on text widths, so place them after a layout pass,
  // and only when a text changed: re-placing a label makes LVGL redraw it, even in the same spot.
  static String placed_for;
  String key = String(lv_label_get_text(s_out_temp)) + "|" + cond;
  if (key != placed_for) {
    placed_for = key;
    lv_obj_update_layout(s_out_temp);
    lv_obj_align_to(s_out_hi, s_out_temp, LV_ALIGN_OUT_RIGHT_TOP, 12, -3);
    lv_obj_align_to(s_out_lo, s_out_temp, LV_ALIGN_OUT_RIGHT_TOP, 12, 21);
    if (*cond)
      lv_obj_align_to(s_out_hum, s_out_sub, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
    else
      lv_obj_align(s_out_hum, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }
  if (o.sunrise_min >= 0) {
    format_hm(o.sunrise_min, buf, sizeof(buf));
    set_text(s_sunrise, buf);
  }
  if (o.sunset_min >= 0) {
    format_hm(o.sunset_min, buf, sizeof(buf));
    set_text(s_sunset, buf);
  }
}

void ui_update() {
  update_clock();
  update_status();
  update_inside();
  update_outside();
  ui_alarms_update();
  ui_sleep_update();
  ui_sunrise_update();
  ui_diag_update();

  if (!lv_obj_has_flag(s_tide_panel, LV_OBJ_FLAG_HIDDEN)) {
    if (millis() - s_tide_opened_ms > kTidePanelMs)
      close_tides();
    else if (!s_scrubbing)  // leave the cursor's reading alone while a finger is on the chart
      update_tides();
  }
}
