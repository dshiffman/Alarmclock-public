#pragma once
#include <stdint.h>

#include "sleep_synth.h"

// Sleep sounds with a timer: rain or ocean, played until the timer runs out (fading out over
// the last couple of minutes), until stopped, or until an alarm rings. The last sound, volume
// and timer are remembered in settings.

static constexpr uint16_t kSleepTimerChoices[] = {15, 30, 60, 120, 0};  // minutes; 0 = all night

void sleep_start();               // with the saved sound, volume and timer
void sleep_stop();                // fades out over a couple of seconds
void sleep_loop();                // call from loop()
bool sleep_active();
// Change while playing (or just save for next time).
void sleep_set_sound(SleepSound s);
void sleep_set_volume(uint8_t percent);
void sleep_set_timer(uint16_t minutes);
// Minutes left, or -1 when there's no timer (all night).
int sleep_minutes_left();
const char *sleep_timer_label(uint16_t minutes);  // "15 min", "1 hour", "All night"
