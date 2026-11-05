#pragma once

// The bass detector, as pure C++ with no Arduino dependency, so the same code
// runs on the helmet and in tools/bass_test on the host.
//
// It is fed one ADC reading per millisecond and everything inside is defined
// in milliseconds. The detector it replaced was stepped once per render frame
// from bursts of back-to-back reads, so its "100 Hz" filters and its flash
// decay both depended on how busy the CPU happened to be -- which is most of
// why it flickered.
//
// The other half of the old problem was that every threshold was relative to
// the signal itself. A MAX9814 turns its gain *up* in silence, so the noise
// floor rose to meet the threshold and the detector fired on its own hiss,
// even with the mic covered. Here the noise gate is an absolute level: while
// the envelope stays under it the output is exactly zero, however the noise
// drifts. Silence is dark by construction, not by tuning.

#include <stdint.h>

namespace BassDsp {

constexpr uint16_t kRateHz = 1000;

// Envelope counts per meter unit. The meter and the gate share these units so
// a gate set "just above the noise on the meter" means exactly that. 255 meter
// units is ~1000 counts of band-passed signal, which a kick through a MAX9814
// reaches comfortably.
constexpr float kMeterDiv = 4.0f;

// Minimum time between kicks. Ten a second is well above any kick pattern in
// music and well below the rate at which a flash reads as flicker.
constexpr uint16_t kRefractoryMs = 100;

// After a kick the detector re-arms only once the envelope has fallen back
// below this share of the trigger level. Time alone is not enough: a long
// kick is still decaying through the trigger when the refractory period ends,
// and without this it fires a second time on its own tail.
constexpr float kRearm = 0.85f;

// Readings discarded after a reset while the DC tracker settles onto the
// mic's bias. Without this the first second after entering Bass mode is one
// long false kick.
constexpr uint16_t kPrimeMs = 300;

enum Band : uint8_t {
  BAND_KICK = 0,  // ~45-110 Hz: the kick drum, ignores bass lines
  BAND_FULL,      // ~40-200 Hz: the whole low end, bass guitar included
  BAND_COUNT
};

struct Params {
  uint8_t sensitivity = 128;  // 0..255: how loud for full brightness
  uint8_t gate = 40;          // meter units: below this the output is dark
  uint8_t kick = 128;         // 0..255: how easily a hit fires a flash
  uint8_t decay = 96;         // 0..255: how long a flash hangs, ~40..700 ms
  uint8_t band = BAND_KICK;
  // Notches out mains hum before the band-pass. Measured on the helmet's
  // MAX9814 with nothing playing, two-thirds of the noise in the kick band was
  // a tone just under 60 Hz plus its 120 Hz rectified-ripple partner; taking
  // them out cut the quiet floor 2.6x. It costs a little of a kick's
  // fundamental, which is why it can be switched off.
  uint8_t hum = 1;
};

// One reading of the detector for the live meter, all in meter units except
// out (0..255) and kicks (a free-running counter, so a client can tell a new
// kick from the same one seen twice).
struct Meter {
  uint8_t level;
  uint8_t trigger;
  uint8_t out;
  uint8_t kicks;
};

// Reduces a burst of back-to-back ADC reads to one sample: drops the lowest
// and the highest and averages the rest. The ESP32's ADC throws occasional
// single-read spikes, worst while the radio transmits, and one of those let
// straight into the band-pass rings like a real hit.
uint16_t trimmedMean(const uint16_t *v, uint8_t n);

// The flash time constant a decay setting maps to, in milliseconds. Exposed so
// a UI can show the same number the detector actually uses.
float decayMs(uint8_t decay);

class Detector {
 public:
  Detector();

  // Forget all history and re-prime. Call when Bass mode is entered.
  void reset();

  // Only ever call this from the thread that calls process(); the firmware
  // hands parameters across threads itself.
  void setParams(const Params &p);
  const Params &params() const { return params_; }

  // One raw ADC reading, 0..4095, taken at kRateHz.
  void process(uint16_t raw);

  uint8_t output() const { return out_; }
  Meter meter() const;

  // For tests and telemetry: the envelope and the current kick threshold, in
  // raw envelope counts.
  float envelope() const { return env_; }
  float triggerLevel() const { return trigger_; }

 private:
  struct Biquad {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float step(float x);
    void clear() { x1 = x2 = y1 = y2 = 0; }
  };

  void configure();

  Params params_;
  Biquad bp_;
  Biquad hum60_;
  Biquad hum120_;

  // Derived from params_ by configure().
  float gateCounts_ = 0;
  float span_ = 1;
  float ratio_ = 1;
  float decayK_ = 0;

  float dc_ = 0;
  float env_ = 0;
  float slow_ = 0;
  float trigger_ = 0;
  float punch_ = 0;
  uint16_t prime_ = 0;
  uint16_t sinceKick_ = 0;
  bool armed_ = true;

  volatile uint8_t out_ = 0;
  volatile uint8_t kicks_ = 0;
};

}  // namespace BassDsp
