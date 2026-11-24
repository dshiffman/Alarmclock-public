// Renders the clock's sleep sounds to WAV files for listening on a computer:
//   g++ -O2 -std=c++17 -Isrc tools/render_sleep.cpp -o render_sleep && ./render_sleep out_dir
// Writes rain.wav and ocean.wav (60 s each, 16 kHz mono, the same samples the clock makes).
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

#include "sleep_synth.h"

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

int main(int argc, char **argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  const int rate = 16000, secs = 60;
  for (int s = 0; s < (int)SleepSound::Count; s++) {
    SleepSynth syn(rate);
    syn.start((SleepSound)s);
    std::vector<float> v(rate * secs);
    for (int i = 0; i < (int)v.size(); i += 256) syn.fill(&v[i], 256);
    double sum = 0, peak = 0;
    for (float x : v) { sum += x * x; peak = fmax(peak, fabs(x)); }
    std::string name = sleep_sound_name((SleepSound)s);
    for (auto &c : name) c = tolower(c);
    const std::string path = dir + "/" + name + ".wav";
    FILE *f = fopen(path.c_str(), "wb");
    fwrite("RIFF", 1, 4, f); put32(f, 36 + v.size() * 2); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, rate); put32(f, rate * 2); put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, v.size() * 2);
    for (float x : v) { int k = (int)lrintf(fmaxf(-1, fminf(1, x * 0.9f)) * 30000); put16(f, (uint16_t)(int16_t)k); }
    fclose(f);
    printf("%s: rms %.3f peak %.3f -> %s\n", name.c_str(), sqrt(sum / v.size()), peak, path.c_str());
  }
}
