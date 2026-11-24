#include "audio.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "board.h"
#include "sleep_synth.h"

static constexpr uint32_t kRate = 16000;
static constexpr int kChunk = 256;                  // stereo frames per write
// The ramp rises evenly in decibels, which ears hear as a steady rise (a straight line in
// amplitude sounds like a jump at the start, then little change). -22 dB is 8 % of the set level.
static constexpr float kRampStartDb = -22.0f;

static I2SClass s_i2s;
static bool s_ok = false;
static volatile bool s_playing = false;
static volatile AlarmSound s_sound = AlarmSound::Chimes;
static volatile uint8_t s_volume = 70;
static volatile bool s_ramp = true;
static volatile bool s_preview = false;  // sped-up, repeating ramp
static volatile uint32_t s_start_sample = 0;
static uint32_t s_sample = 0;  // running sample counter (audio task only)

// Sleep sounds play when no alarm sound is. Their level glides toward s_sleep_target, so
// starting, stopping, volume changes and the timer's fade-out are all smooth.
static volatile bool s_sleep_on = false;
static volatile SleepSound s_sleep_sound = SleepSound::Rain;
static volatile bool s_sleep_restart = false;
static volatile float s_sleep_target = 0;  // 0..1 (already squared for loudness)
static float s_sleep_level = 0;            // audio task only
static constexpr float kSleepGlideS = 2.5f;  // seconds to glide from silent to full

// The song clip: 16 kHz mono 16-bit, read once into PSRAM so playing it never touches the flash
// (reading flash while the screen runs would make it flicker, and the alarm must not stutter).
static int16_t *s_song = nullptr;
static uint32_t s_song_len = 0;  // samples
static char s_song_name[25] = "My song";
static constexpr uint32_t kMaxSongBytes = 3u << 20;

static void load_song() {
  if (!LittleFS.begin(false)) {
    Serial.println("[audio] no file space yet: no song (it plays Chimes)");
    return;
  }
  File f = LittleFS.open("/alarm.pcm", "r");
  if (!f || f.size() < kRate * 2 || f.size() > kMaxSongBytes) {
    Serial.println("[audio] no song file (it plays Chimes)");
    return;
  }
  const size_t bytes = f.size() & ~1u;
  s_song = (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!s_song || f.read((uint8_t *)s_song, bytes) != bytes) {
    Serial.println("[audio] couldn't read the song file (it plays Chimes)");
    free(s_song);
    s_song = nullptr;
    return;
  }
  f.close();
  File n = LittleFS.open("/alarm.txt", "r");
  if (n) {
    const size_t len = n.read((uint8_t *)s_song_name, sizeof(s_song_name) - 1);
    s_song_name[len] = 0;
  }
  s_song_len = bytes / 2;
  Serial.printf("[audio] song \"%s\", %.1f s\n", s_song_name, s_song_len / (float)kRate);
}

const char *audio_sound_name(AlarmSound s) {
  switch (s) {
    case AlarmSound::Chimes: return "Chimes";
    case AlarmSound::Pulse: return "Soft pulse";
    case AlarmSound::Beep: return "Classic beep";
    case AlarmSound::Rise: return "Rising tones";
    case AlarmSound::Song: return s_song_name;
    default: return "";
  }
}

static inline float note_hz(int semitones_from_a4) { return 440.0f * powf(2.0f, semitones_from_a4 / 12.0f); }

// Sounds are made sample by sample, 16,000 a second, so the maths has to be cheap. sinf() and
// expf() were too slow for Chimes (a block took longer to make than to play, and once that
// tripped the watchdog), so both come from tables filled at start-up.
static constexpr int kTab = 1024;
static float s_sin_tab[kTab + 1];   // one cycle of sine
static constexpr float kBellLen = 1.6f;  // seconds a bell note is audible
static float s_bell_tab[kTab + 1];  // exp(-2.2 t) over 0..kBellLen

static inline float lookup(const float *tab, float x) {  // x in 0..kTab, linear interpolation
  const int i = (int)x;
  return tab[i] + (tab[i + 1] - tab[i]) * (x - i);
}
// sin(2 pi * cycles)
static inline float sin_cycles(float cycles) { return lookup(s_sin_tab, (cycles - floorf(cycles)) * kTab); }

// Note pitches, worked out once instead of 16,000 times a second.
static const int kChimeNotes[] = {3, 7, 10, 15, 10, 7};  // C5 E5 G5 C6 G5 E5 (from A4)
static const int kRiseSteps[] = {0, 4, 7, 12, 16};       // from C4
static float s_chime_hz[6], s_rise_hz[5];

// A struck bell: a few inharmonic partials with exponential decay. t is at most 1.6 s, so
// the sine arguments stay small.
static float bell(float t, float hz) {
  if (t < 0 || t >= kBellLen) return 0;  // inaudible after ~1.6 s; skip the math
  const float env = lookup(s_bell_tab, t * (kTab / kBellLen));
  const float env2 = env * env;      // upper partials die away faster
  const float c = hz * t;
  return env * (0.6f * sin_cycles(c) + env2 * (0.25f * sin_cycles(c * 2.76f) + 0.15f * sin_cycles(c * 5.4f)));
}

// Seconds into the current repeat of a sound that repeats every `period_samples`, worked out
// from the whole-number sample count so it stays exact however long the alarm rings.
static inline float phase_time(uint32_t n, uint32_t period_samples) { return (n % period_samples) / (float)kRate; }

// How long each sound takes before it repeats, in seconds (matches synth() below).
static float sound_period_s(AlarmSound sound) {
  switch (sound) {
    case AlarmSound::Chimes: return 4.8f;
    case AlarmSound::Beep: return 1.2f;
    case AlarmSound::Song: return s_song ? s_song_len / (float)kRate : 4.8f;
    default: return 3.0f;  // Pulse, Rise
  }
}

// A preview's ramp: about kPreviewRampSeconds, rounded up to whole repeats of the sound so it
// starts over in the gap between phrases, not in the middle of one.
static float preview_ramp_s(AlarmSound sound) {
  const float p = sound_period_s(sound);
  if (p > kPreviewRampSeconds) return kPreviewRampSeconds;  // a song: just the first 18 s
  return p * ceilf(kPreviewRampSeconds / p - 0.01f);
}

// One sample of a sound, n samples after it started, in -1..1. Each sound is silent at the end
// of its repeat, so restarting the tones there is inaudible.
static float synth(AlarmSound sound, uint32_t n) {
  if (sound == AlarmSound::Song) {
    if (s_song) return s_song[n % s_song_len] * (1.0f / 32768);
    sound = AlarmSound::Chimes;  // no song loaded: the alarm still rings
  }
  switch (sound) {
    case AlarmSound::Chimes: {
      // A slow pentatonic phrase, repeating every 4.8 s.
      const float tp = phase_time(n, kRate * 48 / 10), step = 0.6f;
      float v = 0;
      for (int i = 0; i < 6; i++) v += bell(tp - i * step, s_chime_hz[i]);
      return v * 0.5f;
    }
    case AlarmSound::Pulse: {
      // Three soft swells, then a rest.
      const float tp = phase_time(n, kRate * 3);
      if (tp > 1.8f) return 0;
      const float ph = fmodf(tp, 0.6f) / 0.6f;
      const float env = sin_cycles(ph * 0.5f);  // sin(pi * ph)
      return env * env * sin_cycles(660.0f * tp);
    }
    case AlarmSound::Beep: {
      // Beep-beep-beep-beep, pause. Slightly softened square wave.
      const float tp = phase_time(n, kRate * 12 / 10);
      if (tp > 0.8f || fmodf(tp, 0.2f) > 0.1f) return 0;
      return tanhf(4 * sin_cycles(1800.0f * tp)) * 0.6f;
    }
    case AlarmSound::Rise: {
      // Rising arpeggio, repeating every 3 s.
      const float tp = phase_time(n, kRate * 3);
      const int i = (int)(tp / 0.35f);
      if (i >= 5) return 0;
      const float tn = tp - i * 0.35f;
      const float env = (1 - expf(-tn * 60)) * expf(-tn * 4);
      return env * sin_cycles(s_rise_hz[i] * tp);
    }
    default:
      return 0;
  }
}

static void audio_task(void *) {
  static int16_t buf[kChunk * 2];
  bool was_playing = false;
  uint32_t worst_us = 0;  // slowest chunk to generate, logged when a sound stops
  static SleepSynth sleep_synth(kRate);
  static float sleep_buf[kChunk];
  for (;;) {
    if (!s_playing && s_sleep_on) {
      if (s_sleep_restart) {
        s_sleep_restart = false;
        sleep_synth.start(s_sleep_sound);
      }
      was_playing = true;
      sleep_synth.fill(sleep_buf, kChunk);
      const float step = kChunk / (kRate * kSleepGlideS);
      const float target = s_sleep_target;
      const float from = s_sleep_level;
      s_sleep_level = target > from ? min(target, from + step) : max(target, from - step);
      for (int i = 0; i < kChunk; i++) {
        const float g = from + (s_sleep_level - from) * i / kChunk;  // no steps within a block
        const int16_t smp = (int16_t)(constrain(sleep_buf[i] * g, -1.0f, 1.0f) * 30000);
        buf[2 * i] = smp;
        buf[2 * i + 1] = smp;
      }
      s_i2s.write((const uint8_t *)buf, sizeof(buf));
      if (target <= 0 && s_sleep_level <= 0) s_sleep_on = false;  // faded out: done
      vTaskDelay(1);
      continue;
    }
    if (!s_playing) {
      if (was_playing) {
        // Flush silence so the amp doesn't sit on the last sample.
        memset(buf, 0, sizeof(buf));
        s_i2s.write((const uint8_t *)buf, sizeof(buf));
        was_playing = false;
        Serial.printf("[audio] slowest chunk %lu us of %lu us\n", (unsigned long)worst_us,
                      (unsigned long)(kChunk * 1000000UL / kRate));
        worst_us = 0;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    was_playing = true;
    const AlarmSound sound = s_sound;
    const float vol = s_volume / 100.0f;
    const bool preview = s_preview;
    const float ramp_s = preview ? preview_ramp_s(sound) : kAlarmRampSeconds;
    const uint32_t t0 = micros();
    // Volume for this 16 ms block. The ramp moves a tiny fraction of a decibel per block, far
    // too little to hear as steps, and working it out once per block keeps powf() off the
    // per-sample path.
    float gain = vol * vol;  // perceived loudness is closer to the square of amplitude
    if (s_ramp) {
      const float t = (s_sample - s_start_sample) / (float)kRate;
      const float tr = preview ? fmodf(t, ramp_s) : t;  // a preview starts the fade over each time
      if (tr < ramp_s) gain *= powf(10.0f, kRampStartDb * (1 - tr / ramp_s) / 20);
    }
    for (int i = 0; i < kChunk; i++, s_sample++) {
      const uint32_t n = s_sample - s_start_sample;
      const float v = constrain(synth(sound, n) * gain, -1.0f, 1.0f);
      const int16_t smp = (int16_t)(v * 30000);
      buf[2 * i] = smp;
      buf[2 * i + 1] = smp;
    }
    worst_us = max(worst_us, micros() - t0);
    s_i2s.write((const uint8_t *)buf, sizeof(buf));
    // Always give the rest of this core a moment, even if generating ever falls behind: the
    // watchdog restarts the clock if its idle task doesn't run for 5 s.
    vTaskDelay(1);
  }
}

void audio_init() {
  for (int i = 0; i <= kTab; i++) {
    s_sin_tab[i] = sinf(2 * PI * i / kTab);
    s_bell_tab[i] = expf(-2.2f * kBellLen * i / kTab);
  }
  for (int i = 0; i < 6; i++) s_chime_hz[i] = note_hz(kChimeNotes[i]);
  for (int i = 0; i < 5; i++) s_rise_hz[i] = note_hz(-9 + kRiseSteps[i]);
  load_song();
  s_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
  s_ok = s_i2s.begin(I2S_MODE_STD, kRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  if (!s_ok) {
    Serial.println("[audio] I2S init failed");
    return;
  }
  xTaskCreatePinnedToCore(audio_task, "audio", 4096, nullptr, 3, nullptr, 0);
}

void audio_play(AlarmSound sound, uint8_t volume, bool ramp, bool preview) {
  if (!s_ok) return;
  s_sleep_on = false;  // an alarm (or a preview) ends any sleep sound
  s_sleep_target = 0;
  s_sleep_level = 0;
  s_sound = sound;
  s_volume = volume > 100 ? 100 : volume;
  s_ramp = ramp;
  s_preview = preview;
  s_start_sample = s_sample;
  s_playing = true;
}

void audio_stop() { s_playing = false; }

void audio_sleep_play(SleepSound sound, float level) {
  if (!s_ok || s_playing) return;
  if (!s_sleep_on || sound != s_sleep_sound) {
    s_sleep_sound = sound;
    s_sleep_restart = true;
    if (!s_sleep_on) s_sleep_level = 0;  // glide in from silence
  }
  s_sleep_target = constrain(level, 0.0f, 1.0f);
  s_sleep_on = true;
}

void audio_sleep_fade_out() { s_sleep_target = 0; }

bool audio_sleep_playing() { return s_sleep_on && !s_playing; }


bool audio_playing() { return s_playing; }
