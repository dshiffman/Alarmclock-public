#pragma once
// Rain and ocean sounds for falling asleep, made on the clock from random noise (no sound
// files). Plain C++ with no Arduino calls, so tools/render_sleep.cpp can build the same code on
// a computer and write WAV files to listen to.
//
// The speaker is small (about 28 mm) and plays almost nothing below 300 Hz, so both sounds
// live mostly above that: rain is hiss and patter, the ocean is the wash and fizz of waves.
#include <math.h>
#include <stdint.h>

enum class SleepSound : uint8_t { Rain = 0, Ocean, Count };

inline const char *sleep_sound_name(SleepSound s) {
  switch (s) {
    case SleepSound::Rain: return "Rain";
    case SleepSound::Ocean: return "Ocean";
    default: return "";
  }
}

class SleepSynth {
 public:
  explicit SleepSynth(float rate = 16000) : _rate(rate) {
    _hiss_hi_a = coef(5000);
    _hiss_lo_a = coef(400);
    _hp_a = coef(150);
    _fizz_a = coef(4000);
  }

  // Start a sound from the beginning of its pattern.
  void start(SleepSound s) {
    _sound = s;
    _wave_pos = 0;
    _wave_len = next_wave_len();
    _block_left = 0;
  }

  // Fills n mono samples (about -1..1). Call with blocks of up to a few hundred samples: the
  // slow changes (rain heaviness, wave swell) are worked out once per call.
  void fill(float *out, int n) {
    if (_sound == SleepSound::Ocean)
      ocean(out, n);
    else
      rain(out, n);
  }

 private:
  float _rate;
  SleepSound _sound = SleepSound::Rain;
  uint32_t _rng = 0x9E3779B9u;

  // Rain
  float _hiss_hi = 0, _hiss_hi2 = 0, _hiss_lo = 0, _hiss_hi_a, _hiss_lo_a;
  float _heavy = 0.8f, _heavy_target = 0.8f;
  int _block_left = 0;
  float _d1 = 0, _d2 = 0, _dc = 0, _dr2 = 0;  // one ringing drop (2-pole resonator)
  float _hp = 0, _hp_a;

  // Ocean
  float _o1 = 0, _o2 = 0, _fizz = 0, _fizz_a;
  float _wave_pos = 0, _wave_len = 10, _wave_size = 1;

  float coef(float hz) const { return 1.0f - expf(-2.0f * 3.14159265f * hz / _rate); }

  // White noise in -1..1 (xorshift: cheap and plenty random for sound).
  inline float noise() {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return (int32_t)_rng * (1.0f / 2147483648.0f);
  }
  inline float rand01() { return (noise() + 1) * 0.5f; }

  void rain(float *out, int n) {
    // Heaviness drifts every few seconds, so it sounds like weather rather than a fan.
    if ((_block_left -= n) <= 0) {
      _block_left = (int)(_rate * (2 + 4 * rand01()));
      _heavy_target = 0.55f + 0.45f * rand01();
    }
    _heavy += (_heavy_target - _heavy) * (n / (_rate * 3));
    const float drop_p = (25 + 70 * _heavy) / _rate;  // drops per sample
    for (int i = 0; i < n; i++) {
      const float w = noise();
      _hiss_hi += _hiss_hi_a * (w - _hiss_hi);
      _hiss_hi2 += _hiss_hi_a * (_hiss_hi - _hiss_hi2);  // second stage: softer top end
      _hiss_lo += _hiss_lo_a * (w - _hiss_lo);
      const float hiss = (_hiss_hi2 - _hiss_lo) * 1.9f;
      // A drop: a short ping at a random pitch, some close and low, most far and high.
      float x = 0;
      if (rand01() < drop_p) {
        const bool is_near = rand01() < 0.12f;
        const float hz = is_near ? 700 + 900 * rand01() : 1800 + 3200 * rand01();
        const float r = is_near ? 0.9965f : 0.993f;
        _dc = 2 * r * cosf(2 * 3.14159265f * hz / _rate);
        _dr2 = r * r;
        x = (is_near ? 0.2f : 0.1f) * (0.3f + 0.7f * rand01());
      }
      const float d = _dc * _d1 - _dr2 * _d2 + x;
      _d2 = _d1;
      _d1 = d;
      float v = hiss * _heavy * 0.4f + d * 0.35f;
      _hp += _hp_a * (v - _hp);  // drop the lows the speaker can't play anyway
      out[i] = v - _hp;
    }
  }

  float next_wave_len() { return 7.5f + 5.5f * rand01(); }

  void ocean(float *out, int n) {
    // One wave: builds over ~35 % of its length, breaks, then washes out slowly. A low
    // background of distant surf never quite stops.
    const float u = _wave_pos / _wave_len;
    float env, bright;
    if (u < 0.35f) {
      const float k = u / 0.35f;
      env = k * k * (3 - 2 * k);
      bright = env;
    } else {
      const float k = (u - 0.35f) / 0.65f;
      env = expf(-3.2f * k);
      bright = expf(-5.0f * k);
    }
    env = 0.12f + 0.88f * env * _wave_size;
    // Fizz of the breaking wave: grows to the crest, then fades with the wash.
    const float fizz_amt = 0.5f * _wave_size * (u < 0.35f ? bright * bright * bright : bright);
    const float a = coef(250 + 2200 * bright);
    _wave_pos += n / _rate;
    if (_wave_pos >= _wave_len) {
      _wave_pos = 0;
      _wave_len = next_wave_len();
      _wave_size = 0.6f + 0.4f * rand01();
    }
    for (int i = 0; i < n; i++) {
      const float w = noise();
      _o1 += a * (w - _o1);  // two gentle low-pass stages: dark when calm, brighter as it breaks
      _o2 += a * (_o1 - _o2);
      _fizz += _fizz_a * (w - _fizz);
      float v = _o2 * 1.15f * env + (w - _fizz) * fizz_amt * 0.6f;
      _hp += _hp_a * (v - _hp);
      out[i] = v - _hp;
    }
  }
};
