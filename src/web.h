#pragma once

// Settings page at http://alarmclock.local (or the clock's IP), for any phone or computer on
// the same WiFi: alarms, screen and night mode, clock and units, location and stations. The
// page itself is web/index.html, compressed into the firmware at build time.
//
// JSON interface used by the page:
//   GET  /api/state           everything the page shows
//   POST /api/settings        {key: value, ...}  any of the keys in "s" from /api/state
//   POST /api/alarm           {i, on, h, m, days, snd, vol, snz, ramp, skip}  i = -1 adds one
//   POST /api/alarm/delete    {i}
//   POST /api/preview         {snd, vol, ramp} plays a sound for a few seconds (with ramp, the
//                             fade-up sped up and repeating); {stop: true} stops it
// Each POST answers with the new state.

// The server runs on its own task, so a slow or stuck client can never hold up the alarms,
// or screen. Its handlers change alarms, settings and the alarm screens, which
// the main loop uses too, so the main loop holds this lock while it works (releasing it for
// its short pause each cycle) and handlers hold it only while they run, not while sending.
void web_init();  // call once in setup(); the server starts when the clock is online
void main_lock();
void main_unlock();
