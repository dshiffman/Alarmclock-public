#pragma once
#include <stdint.h>

#include "sleep_synth.h"

// Alarm sounds, synthesized on the clock and played through the board's built-in I2S speaker
// amp. Song is your own clip, loaded from the clock's file space at start-up
// (tools/make_alarm_song.py makes it); without one it plays Chimes.

enum class AlarmSound : uint8_t { Chimes = 0, Pulse, Beep, Rise, Song, Count };

const char *audio_sound_name(AlarmSound s);

void audio_init();

// Starts a sound looping. volume 0-100. With ramp, it starts quiet and reaches full volume
// over a minute. A preview (preview = true) speeds the ramp up and repeats it, quiet to full
// every few seconds, so the fade can be heard without waiting.
static constexpr float kAlarmRampSeconds = 60.0f;
// Rounded up to whole repeats of the sound: 4 Chimes phrases (19.2 s), 6 Pulse/Rise cycles.
// Shorter sounded like two levels rather than a steady rise.
static constexpr float kPreviewRampSeconds = 18.0f;
void audio_play(AlarmSound sound, uint8_t volume, bool ramp, bool preview = false);
void audio_stop();
bool audio_playing();

// Sleep sounds (rain, ocean). level 0..1 is the loudness to glide to (call again to change it
// or to switch sound). An alarm sound or preview ends a sleep sound. audio_stop() doesn't:
// fade it out instead, and it switches itself off when silent.
void audio_sleep_play(SleepSound sound, float level);
void audio_sleep_fade_out();
bool audio_sleep_playing();
