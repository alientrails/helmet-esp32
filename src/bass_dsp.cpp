#include "bass_dsp.h"

#include <math.h>

namespace BassDsp {
namespace {

constexpr float kPi = 3.14159265f;

// DC tracker: follows the MAX9814's ~1.25 V bias with a ~3 Hz corner, far
// below the band of interest.
const float kDcK = 1.0f - expf(-2.0f * kPi * 3.0f / kRateHz);

// Envelope: fast attack so the leading edge of a kick is caught in a couple of
// milliseconds, a slower release so the 150 Hz ripple of a rectified 75 Hz
// tone does not read as a string of separate hits.
const float kAttackK = 1.0f - expf(-1.0f / 2.0f);
const float kReleaseK = 1.0f - expf(-1.0f / 60.0f);

// The running average a kick has to stand out from. Long enough to span a few
// beats, so a kick is measured against the music and not against itself.
const float kSlowK = 1.0f - expf(-1.0f / 400.0f);

// Floor under which the output is snapped to zero, so a fading flash ends on
// black instead of an 8-bit crawl through the last few levels.
constexpr float kOutFloor = 0.03f;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Exponential mapping of a 0..255 control onto [lo, hi]. Used for anything
// whose useful range spans an order of magnitude, where a linear slider would
// spend most of its travel on one end.
float expMap(uint8_t v, float lo, float hi) { return lo * powf(hi / lo, v / 255.0f); }

}  // namespace

uint16_t trimmedMean(const uint16_t *v, uint8_t n) {
  if (n == 0) {
    return 0;
  }
  if (n < 3) {
    return v[0];
  }
  uint32_t sum = 0;
  uint16_t lo = 0xFFFF;
  uint16_t hi = 0;
  for (uint8_t i = 0; i < n; i++) {
    sum += v[i];
    if (v[i] < lo) {
      lo = v[i];
    }
    if (v[i] > hi) {
      hi = v[i];
    }
  }
  return (uint16_t)((sum - lo - hi + (n - 2) / 2) / (n - 2));
}

float decayMs(uint8_t decay) { return expMap(decay, 40.0f, 700.0f); }

float Detector::Biquad::step(float x) {
  const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = y;
  return y;
}

Detector::Detector() {
  configure();
  reset();
}

void Detector::reset() {
  bp_.clear();
  hum60_.clear();
  hum120_.clear();
  dc_ = 0.0f;
  env_ = 0.0f;
  slow_ = 0.0f;
  trigger_ = gateCounts_;
  punch_ = 0.0f;
  prime_ = 0;
  sinceKick_ = 0xFFFF;
  armed_ = true;
  out_ = 0;
}

void Detector::setParams(const Params &p) {
  const bool bandChanged = p.band != params_.band || (p.hum != 0) != (params_.hum != 0);
  params_ = p;
  if (params_.band >= BAND_COUNT) {
    params_.band = BAND_KICK;
  }
  configure();
  if (bandChanged) {
    // New coefficients against the old filter state ring for a few cycles,
    // which would fire a kick nobody played. Clear it and settle briefly;
    // prime_ stays non-zero so the DC tracker keeps its lock on the bias.
    bp_.clear();
    hum60_.clear();
    hum120_.clear();
    prime_ = 1;
  }
}

void Detector::configure() {
  // Constant-0 dB-peak band-pass (RBJ cookbook), so the envelope reads the
  // same at the centre of either band.
  const float f0 = params_.band == BAND_FULL ? 95.0f : 75.0f;
  const float q = params_.band == BAND_FULL ? 0.6f : 1.2f;
  const float w0 = 2.0f * kPi * f0 / kRateHz;
  const float alpha = sinf(w0) / (2.0f * q);
  const float a0 = 1.0f + alpha;
  bp_.b0 = alpha / a0;
  bp_.b1 = 0.0f;
  bp_.b2 = -alpha / a0;
  bp_.a1 = -2.0f * cosf(w0) / a0;
  bp_.a2 = (1.0f - alpha) / a0;

  // Hum notches (RBJ). The first is a little under 60 Hz and fairly wide
  // because that is where the measured hum actually sat, smeared across
  // 56-60 Hz rather than on a clean line; the 120 Hz one is narrow because
  // that partner was exact.
  auto notch = [](Biquad &f, float hz, float q) {
    const float w = 2.0f * kPi * hz / kRateHz;
    const float al = sinf(w) / (2.0f * q);
    const float n = 1.0f + al;
    f.b0 = 1.0f / n;
    f.b1 = -2.0f * cosf(w) / n;
    f.b2 = 1.0f / n;
    f.a1 = -2.0f * cosf(w) / n;
    f.a2 = (1.0f - al) / n;
  };
  notch(hum60_, 58.5f, 4.0f);
  notch(hum120_, 120.0f, 8.0f);

  gateCounts_ = params_.gate * kMeterDiv;

  // Envelope above the gate that reaches full brightness: 900 counts at the
  // least sensitive, 60 at the most.
  span_ = expMap(params_.sensitivity, 900.0f, 60.0f);

  // How far above its running average the envelope must jump to count as a
  // kick: 2.6x at 0 (only clear hits), 1.15x at 255 (nearly everything).
  ratio_ = 2.6f - 1.45f * (params_.kick / 255.0f);

  // Flash time constant, 40 ms at 0 to 700 ms at 255.
  decayK_ = expf(-1.0f / decayMs(params_.decay));
}

void Detector::process(uint16_t raw) {
  const float x = (float)raw;
  if (prime_ == 0) {
    // Start the tracker on the bias rather than at zero, or the first second
    // is one long false kick while it climbs.
    dc_ = x;
  }
  dc_ += kDcK * (x - dc_);
  float ac = x - dc_;
  if (params_.hum != 0) {
    ac = hum120_.step(hum60_.step(ac));
  }
  const float y = bp_.step(ac);
  const float rect = y < 0.0f ? -y : y;
  env_ += (rect > env_ ? kAttackK : kReleaseK) * (rect - env_);

  if (prime_ < kPrimeMs) {
    prime_++;
    slow_ = env_;
    trigger_ = gateCounts_;
    punch_ = 0.0f;
    out_ = 0;
    return;
  }

  slow_ += kSlowK * (env_ - slow_);
  if (sinceKick_ < 0xFFFF) {
    sinceKick_++;
  }

  // A kick has to clear both the absolute gate and the music's own running
  // level. The gate is what keeps silence dark; the ratio is what lets a kick
  // stand out from a loud mix.
  const float rel = slow_ * ratio_;
  trigger_ = rel > gateCounts_ ? rel : gateCounts_;
  if (!armed_ && env_ < trigger_ * kRearm) {
    armed_ = true;
  }
  if (armed_ && env_ > trigger_ && sinceKick_ >= kRefractoryMs) {
    punch_ = 1.0f;
    sinceKick_ = 0;
    armed_ = false;
    kicks_ = (uint8_t)(kicks_ + 1);
  }
  punch_ *= decayK_;

  const float level = clamp01((env_ - gateCounts_) / span_);
  float out = level > punch_ ? level : punch_;
  if (out < kOutFloor) {
    out = 0.0f;
  }
  out_ = (uint8_t)(out * 255.0f + 0.5f);
}

Meter Detector::meter() const {
  auto units = [](float counts) -> uint8_t {
    const float m = counts / kMeterDiv;
    return m >= 255.0f ? 255 : (m <= 0.0f ? 0 : (uint8_t)(m + 0.5f));
  };
  return Meter{units(env_), units(trigger_), out_, kicks_};
}

}  // namespace BassDsp
