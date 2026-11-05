// Host test for the bass detector (src/bass_dsp.cpp).
//
//   make && ./bass_test
//   ./bass_test replay capture.log ...   # real captures from the 'r' command
//
// Both the detector the firmware used to ship and the new one are run against
// the same simulated mic signal, each sampled the way its firmware actually
// samples: the old code read 28 back-to-back samples once per render frame
// (every ~4 ms here), the new code reads 4 back-to-back once per millisecond.
// The signal underneath is a continuous 50 kHz stream, so both see the same
// sound and the only difference is the detector.
//
// The old detector is reproduced verbatim from the firmware so the comparison
// is honest: it is the code that was flashing with the mic covered.

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "bass_dsp.h"

namespace {

constexpr int kStreamHz = 50000;  // back-to-back analogRead rate, ~20 us each
constexpr int kSeconds = 10;
constexpr float kBias = 1376.0f;  // measured MAX9814 DC level on the CYD

// ---------------------------------------------------------------- signal

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (uint32_t)(s >> 32);
  }
  float uniform() { return (next() + 0.5f) / 4294967296.0f; }
  float gauss() {
    const float u = uniform(), v = uniform();
    return sqrtf(-2.0f * logf(u)) * cosf(6.2831853f * v);
  }
};

struct Kick {
  float atSec;
};

struct Scenario {
  const char *name;
  float noiseSd = 0;        // broadband noise, ADC counts
  float spikeEveryMs = 0;   // single-read ADC glitches, 0 = none
  float spikeSize = 0;
  float toneHz = 0;         // a continuous non-bass tone, 0 = none
  float toneAmp = 0;
  float bpm = 0;            // kick drum, 0 = none
  float kickAmp = 0;
  bool expectSilent = false;
  float humAmp = 0;         // mains hum as measured on the helmet: a tone a
                            // little under 60 Hz plus a 120 Hz partner
};

std::vector<uint16_t> synth(const Scenario &sc, std::vector<Kick> *kicks) {
  Rng rng(0xB455);
  const int n = kStreamHz * kSeconds;
  std::vector<uint16_t> out(n);
  const int spikeEvery = sc.spikeEveryMs > 0 ? (int)(sc.spikeEveryMs * kStreamHz / 1000) : 0;
  const float beat = sc.bpm > 0 ? 60.0f / sc.bpm : 0.0f;
  if (beat > 0) {
    // First kick after the detector has primed, so every planted kick counts.
    for (float t = 0.5f; t < kSeconds - 0.2f; t += beat) {
      kicks->push_back({t});
    }
  }
  for (int i = 0; i < n; i++) {
    const float t = (float)i / kStreamHz;
    float v = kBias + sc.noiseSd * rng.gauss();
    if (sc.toneHz > 0) {
      v += sc.toneAmp * sinf(6.2831853f * sc.toneHz * t);
    }
    if (beat > 0) {
      // A kick drum: a 60 Hz thump with a fast exponential decay. Only the
      // kicks recorded in *kicks are sounded, so every kick the detector can
      // hear is one the test knows about.
      const float since = fmodf(t - 0.5f, beat);
      const float onset = t - since;
      if (t >= 0.5f && since < 0.4f && onset <= kicks->back().atSec + 1e-4f) {
        // A real kick is not a steady tone: it starts around 130 Hz and drops
        // to ~55 Hz within a few tens of milliseconds. Modelling that matters
        // here, because a steady 60 Hz kick would sit right in the hum notch
        // and make it look far more costly than it is.
        const float phase = 6.2831853f * (55.0f * since + 75.0f * 0.03f * (1.0f - expf(-since / 0.03f)));
        v += sc.kickAmp * expf(-since / 0.09f) * sinf(phase);
      }
    }
    if (sc.humAmp > 0) {
      v += sc.humAmp * sinf(6.2831853f * 59.0f * t) + 0.4f * sc.humAmp * sinf(6.2831853f * 120.0f * t);
    }
    if (spikeEvery > 0 && i % spikeEvery == 0) {
      v += sc.spikeSize;
    }
    out[i] = v < 0 ? 0 : (v > 4095 ? 4095 : (uint16_t)v);
  }
  return out;
}

// ---------------------------------------------------------- old detector

// Verbatim from src/leds.cpp before the rewrite, constants from config.h.
struct OldDetector {
  float micDc = 2048, micBass = 0, micSub = 0, micEnv = 0;
  float micFloor = 40, micSlow = 40, micPunch = 0;
  uint16_t micPrime = 0;
  int punches = 0;

  uint8_t frame(const uint16_t *burst) {
    for (int i = 0; i < 28; i++) {
      const float x = burst[i];
      micDc += 0.003f * (x - micDc);
      const float ac = x - micDc;
      micBass += 0.05f * (ac - micBass);
      micSub += 0.015f * (ac - micSub);
      float kick = micBass - micSub;
      if (kick < 0) kick = -kick;
      if (kick > micEnv) micEnv += 0.40f * (kick - micEnv);
      else micEnv += 0.055f * (kick - micEnv);
    }
    if (micPrime < 60) {
      micPrime++;
      micFloor = micEnv;
      micSlow = micEnv;
      return 0;
    }
    micFloor += 0.004f * (micEnv - micFloor);
    micSlow += 0.02f * (micEnv - micSlow);
    if (micEnv > micSlow * 1.48f && micEnv > micFloor + 10.0f) {
      if (micPunch < 0.5f) punches++;
      micPunch = 1.0f;
    }
    micPunch *= 0.91f;
    const float thresh = micFloor * 1.25f + 10.0f;
    const float range = micFloor * 3.5f + 35.0f;
    float follow = (micEnv - thresh) / range;
    if (follow < 0) follow = 0;
    if (follow > 1) follow = 1;
    float out = follow > micPunch ? follow : micPunch;
    if (out < 0.04f) out = 0;
    return (uint8_t)(out * 255.0f + 0.5f);
  }
};

// ----------------------------------------------------------------- runs

struct Result {
  int kicks = 0;
  float litPct = 0;     // share of time the strip was lit at all
  float edgesPerSec = 0;  // dark<->lit transitions: the flicker measure
  int matched = 0;      // kicks within 60 ms after a planted kick
  int stray = 0;        // kicks nowhere near a planted one
  std::vector<float> strayAfterMs;  // for each stray: ms after the last planted kick
};

Result runOld(const std::vector<uint16_t> &s) {
  OldDetector d;
  const int frameEvery = kStreamHz * 4 / 1000;  // a 4 ms render frame
  int frames = 0, lit = 0, edges = 0;
  bool wasLit = false;
  for (size_t i = 0; i + 28 < s.size(); i += frameEvery) {
    frames++;
    const bool on = d.frame(&s[i]) > 0;
    if (on) lit++;
    if (on != wasLit) edges++;
    wasLit = on;
  }
  Result r;
  r.kicks = d.punches;
  r.litPct = 100.0f * lit / frames;
  r.edgesPerSec = (float)edges / kSeconds;
  return r;
}

Result runNew(const std::vector<uint16_t> &s, const std::vector<Kick> &planted,
              const BassDsp::Params &p) {
  BassDsp::Detector d;
  d.setParams(p);
  d.reset();
  const int tickEvery = kStreamHz / 1000;
  int ticks = 0, lit = 0, edges = 0;
  bool wasLit = false;
  uint8_t lastKicks = 0;
  Result r;
  for (size_t i = 0; i + 4 < s.size(); i += tickEvery) {
    d.process(BassDsp::trimmedMean(&s[i], 4));
    ticks++;
    const bool on = d.output() > 0;
    if (on) lit++;
    if (on != wasLit) edges++;
    wasLit = on;
    const uint8_t k = d.meter().kicks;
    if (k != lastKicks) {
      lastKicks = k;
      r.kicks++;
      const float t = (float)i / kStreamHz;
      bool near = false;
      for (const Kick &pk : planted) {
        if (t >= pk.atSec && t - pk.atSec < 0.060f) near = true;
      }
      if (near) {
        r.matched++;
      } else {
        r.stray++;
        float last = -1;
        for (const Kick &pk : planted) {
          if (pk.atSec <= t) last = pk.atSec;
        }
        r.strayAfterMs.push_back(last < 0 ? -1.0f : (t - last) * 1000.0f);
      }
    }
  }
  r.litPct = 100.0f * lit / ticks;
  r.edgesPerSec = (float)edges / kSeconds;
  return r;
}

int failures = 0;

void check(bool ok, const char *what) {
  printf("    %s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

}  // namespace

// Runs real captures from the helmet ('r' on the serial console, saved to a
// file) through the detector. Each RAW line is two seconds of the exact 1 kHz
// samples the firmware's detector saw.
int replay(int argc, char **argv) {
  for (int a = 2; a < argc; a++) {
    FILE *f = fopen(argv[a], "rb");
    if (f == nullptr) {
      fprintf(stderr, "cannot open %s\n", argv[a]);
      return 2;
    }
    std::string txt;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) txt.append(chunk, got);
    fclose(f);
    const size_t at = txt.find("RAW ");
    if (at == std::string::npos) {
      printf("%s: no RAW capture in file\n", argv[a]);
      continue;
    }
    const size_t colon = txt.find(':', at);
    std::vector<uint16_t> x;
    for (size_t i = colon + 1; i < txt.size() && txt[i] != '\n' && txt[i] != '\r';) {
      while (i < txt.size() && txt[i] == ' ') i++;
      if (i >= txt.size() || txt[i] < '0' || txt[i] > '9') break;
      x.push_back((uint16_t)strtoul(&txt[i], nullptr, 10));
      while (i < txt.size() && txt[i] >= '0' && txt[i] <= '9') i++;
    }
    printf("%s: %zu samples\n", argv[a], x.size());
    for (int hum = 1; hum >= 0; hum--) {
      BassDsp::Params p;
      p.hum = (uint8_t)hum;
      p.gate = 255;  // measure the floor without the gate shaping anything
      BassDsp::Detector d;
      d.setParams(p);
      d.reset();
      std::vector<float> env;
      for (uint16_t v : x) {
        d.process(v);
        env.push_back(d.envelope() / BassDsp::kMeterDiv);
      }
      env.erase(env.begin(), env.begin() + BassDsp::kPrimeMs);
      std::vector<float> sorted = env;
      std::sort(sorted.begin(), sorted.end());
      auto pct = [&](float q) { return sorted[(size_t)(q * (sorted.size() - 1))]; };
      printf("    hum filter %-3s  envelope (meter units) median %5.1f  p99 %5.1f  max %5.1f\n",
             hum ? "on" : "off", pct(0.5f), pct(0.99f), sorted.back());
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 3 && std::string(argv[1]) == "replay") {
    return replay(argc, argv);
  }
  const BassDsp::Params defaults;
  printf("bass detector host test -- %d s per scenario, defaults: sens %u gate %u kick %u decay %u\n\n",
         kSeconds, defaults.sensitivity, defaults.gate, defaults.kick, defaults.decay);

  const Scenario scenarios[] = {
      {"silence, quiet mic (sd 20)", 20, 0, 0, 0, 0, 0, 0, true},
      {"silence, MAX9814 at full gain (sd 60)", 60, 0, 0, 0, 0, 0, 0, true},
      {"silence + ADC/radio glitches every 30 ms", 60, 30, 900, 0, 0, 0, 0, true},
      {"voice-band tone, 440 Hz, no kick", 60, 0, 0, 440, 400, 0, 0, true},
      {"kick drum, 120 BPM, over noise", 60, 0, 0, 0, 0, 120, 700, false},
      {"kick drum, 128 BPM, quiet and over a 440 Hz tone", 60, 0, 0, 440, 250, 128, 350, false},
      {"silence with mains hum, as measured on the helmet", 40, 0, 0, 0, 0, 0, 0, true, 160},
      {"kick drum, 120 BPM, over mains hum", 40, 0, 0, 0, 0, 120, 600, false, 160},
  };

  for (const Scenario &sc : scenarios) {
    std::vector<Kick> planted;
    const std::vector<uint16_t> s = synth(sc, &planted);
    const Result o = runOld(s);
    const Result n = runNew(s, planted, defaults);
    BassDsp::Params noHum = defaults;
    noHum.hum = 0;
    const Result nh = runNew(s, planted, noHum);

    printf("%s\n", sc.name);
    printf("    old: %3d kicks, lit %5.1f%% of the time, %5.1f on/off edges/s\n", o.kicks,
           o.litPct, o.edgesPerSec);
    printf("    new: %3d kicks, lit %5.1f%% of the time, %5.1f on/off edges/s", n.kicks,
           n.litPct, n.edgesPerSec);
    if (!planted.empty()) {
      printf("\n         %d of %zu planted kicks caught, %d stray", n.matched, planted.size(),
             n.stray);
      for (float ms : n.strayAfterMs) printf(" [+%.0f ms]", ms);
    }
    printf("\n");
    printf("    new, hum filter off: %3d kicks, lit %5.1f%%", nh.kicks, nh.litPct);
    if (!planted.empty()) printf(", %d of %zu caught, %d stray", nh.matched, planted.size(), nh.stray);
    printf("\n");

    if (sc.expectSilent) {
      check(n.kicks == 0 && n.litPct == 0.0f, "new detector stays dark");
    } else {
      check(n.matched >= (int)planted.size() - 1, "new detector catches the kicks");
      check(n.stray == 0, "no stray or doubled kicks");
    }
    printf("\n");
  }

  // The gate is a hard guarantee, not a tendency: drive the envelope up to
  // just under it and the output must stay exactly zero.
  {
    printf("gate is absolute\n");
    BassDsp::Params p;
    p.gate = 255;   // ~1020 counts
    BassDsp::Detector d;
    d.setParams(p);
    d.reset();
    Rng rng(7);
    int lit = 0;
    float maxEnv = 0;
    for (int i = 0; i < 20000; i++) {
      const float t = i / 1000.0f;
      const float v = kBias + 150.0f * rng.gauss() + 600.0f * sinf(6.2831853f * 70.0f * t);
      d.process((uint16_t)(v < 0 ? 0 : (v > 4095 ? 4095 : v)));
      if (d.output() > 0) lit++;
      if (d.envelope() > maxEnv) maxEnv = d.envelope();
    }
    printf("    envelope peaked at %.0f counts against a gate of %.0f\n", maxEnv,
           p.gate * BassDsp::kMeterDiv);
    check(maxEnv < p.gate * BassDsp::kMeterDiv && lit == 0,
          "a loud 70 Hz tone under the gate never lights the strip");
    printf("\n");
  }

  // Decay is defined in time, so a flash must fall at the rate the control
  // promises regardless of anything else going on.
  {
    printf("decay is time-based\n");
    BassDsp::Params p;
    p.sensitivity = 0;  // keep the follow level out of the way
    p.decay = 0;        // ~40 ms
    BassDsp::Detector d;
    d.setParams(p);
    d.reset();
    for (int i = 0; i < 400; i++) d.process((uint16_t)kBias);
    // One sharp hit, then silence.
    for (int i = 0; i < 40; i++) {
      d.process((uint16_t)(kBias + 900.0f * sinf(6.2831853f * 70.0f * i / 1000.0f)));
    }
    int ms = 0;
    while (d.output() > 0 && ms < 2000) {
      d.process((uint16_t)kBias);
      ms++;
    }
    printf("    a flash at decay 0 is dark %d ms after the hit\n", ms);
    check(ms > 40 && ms < 400, "fades out in a few time constants, not instantly or never");
    printf("\n");
  }

  {
    printf("trimmed mean drops a glitch\n");
    const uint16_t burst[6] = {1376, 1380, 1372, 2300, 1378, 1374};
    const uint16_t m = BassDsp::trimmedMean(burst, 6);
    printf("    burst with one 2300 spike -> %u\n", m);
    check(m >= 1372 && m <= 1380, "the spike is discarded");
    printf("\n");
  }

  printf("%s: %d failure%s\n", failures ? "FAILED" : "ok", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
