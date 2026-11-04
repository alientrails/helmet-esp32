#include "leds.h"

#include <FastLED.h>
#include <math.h>

#include "config.h"

namespace Leds {
namespace {

// Two buffers on purpose. Patterns paint gPixels in plain logical RGB and never
// think about wiring; writeWire() permutes into leds[] just before the frame
// goes out, which is what makes the colour order a runtime setting.
CRGB gPixels[LED_COUNT_MAX];
CRGB leds[LED_COUNT_MAX];

uint8_t gMode = MODE_SOLID;
uint8_t gBrightness = BRIGHTNESS_DEFAULT;
bool gOn = true;
uint8_t lastDuty = 0;
CRGB gColor = CRGB::White;

uint8_t gSpeed = SPEED_DEFAULT;
uint8_t gOrder = LED_ORDER_DEFAULT;
uint16_t gCount = LED_COUNT;
uint16_t gMaxMa = LED_MAX_MA;
bool gStarted = false;

// Scaled animation clock, in milliseconds-at-1x. Every pattern is a pure
// function of this, so changing the speed stretches time smoothly instead of
// jumping the animation, and a slow frame never shortens a blink.
uint32_t gPhase = 0;
uint32_t gPhaseAt = 0;
uint16_t gPhaseRem = 0;  // leftover 1/SPEED_NORMAL ticks, carried between frames

float micDc = 2048.0f;
float micBass = 0.0f;
float micSub = 0.0f;
float micEnv = 0.0f;
float micFloor = 40.0f;
float micSlow = 40.0f;
float micPunch = 0.0f;
uint16_t micPrime = 0;

const char *kNames[MODE_COUNT] = {
    "Solid",   "Breathe", "Heartbeat", "Slow blink", "Fast blink", "Strobe",
    "SOS",     "Party",   "Bass",      "Comet",      "Scanner",    "Twinkle",
    "Fire",    "Rainbow", "Wipe",      "Chase"};

const char *kOrderNames[ORDER_COUNT] = {"GRB", "RGB", "BRG", "RBG", "GBR", "BGR"};

// Which logical channel (0=r, 1=g, 2=b) goes out first, second and third.
const uint8_t kOrderMap[ORDER_COUNT][3] = {
    {1, 0, 2},  // GRB
    {0, 1, 2},  // RGB
    {2, 0, 1},  // BRG
    {0, 2, 1},  // RBG
    {1, 2, 0},  // GBR
    {2, 1, 0},  // BGR
};

// Sub-pixel steps per LED. Moving heads are tracked in these units so a comet
// crawling at a few pixels a second still looks smooth rather than steppy.
constexpr uint16_t kSub = 16;

// Cheap integer hash, used to give each pixel its own twinkle phase and rate
// without a stateful RNG — the patterns have to stay pure functions of gPhase.
uint8_t hash8(uint16_t x) {
  uint32_t h = (uint32_t)x * 2654435761u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return (uint8_t)(h >> 24);
}

void advancePhase(uint32_t now) {
  uint32_t dt = now - gPhaseAt;
  gPhaseAt = now;
  // A long stall (flash write, BLE connect) shouldn't fast-forward the
  // animation by half a second when it clears.
  if (dt > 250u) {
    dt = 250u;
  }
  const uint32_t ticks = dt * (uint32_t)gSpeed + gPhaseRem;
  gPhase += ticks / SPEED_NORMAL;
  gPhaseRem = (uint16_t)(ticks % SPEED_NORMAL);
}

uint8_t patternDuty(uint32_t now) {
  switch (gMode) {
    case MODE_SOLID:
      return 255;
    case MODE_BREATHE: {
      const float t = (now % 4000u) / 4000.0f;
      const float s = 0.5f - 0.5f * cosf(t * 2.0f * (float)M_PI);
      return (uint8_t)(s * 255.0f);
    }
    case MODE_HEARTBEAT: {
      const uint32_t p = now % 1200u;
      if (p < 90 || (p >= 180 && p < 270)) {
        return 255;
      }
      return 0;
    }
    case MODE_BLINK_SLOW:
      return ((now / 400u) % 2u) ? 255 : 0;
    case MODE_BLINK_FAST:
      return ((now / 120u) % 2u) ? 255 : 0;
    case MODE_STROBE:
      return ((now % 80u) < 18u) ? 255 : 0;
    case MODE_SOS: {
      static const uint8_t seq[] = {1, 0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0,
                                    1, 1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0};
      const uint32_t unit = 160;
      const uint32_t idx = (now / unit) % (sizeof(seq) / sizeof(seq[0]));
      return seq[idx] ? 255 : 0;
    }
    // The per-pixel patterns below shade themselves, so they ask for a full
    // frame and do the dimming in their own render function.
    case MODE_PARTY:
    case MODE_COMET:
    case MODE_SCANNER:
    case MODE_TWINKLE:
    case MODE_FIRE:
    case MODE_RAINBOW:
    case MODE_WIPE:
    case MODE_CHASE:
      return 255;
    default:
      return 0;
  }
}

void resetMic() {
  micDc = 2048.0f;
  micBass = 0.0f;
  micSub = 0.0f;
  micEnv = 0.0f;
  micFloor = 40.0f;
  micSlow = 40.0f;
  micPunch = 0.0f;
  micPrime = 0;
}

uint8_t bassDuty() {
  for (int i = 0; i < MIC_BURST; i++) {
    const float x = (float)analogRead(PIN_MIC);
    micDc += MIC_DC_ALPHA * (x - micDc);
    const float ac = x - micDc;
    micBass += MIC_BASS_ALPHA * (ac - micBass);
    micSub += MIC_SUB_ALPHA * (ac - micSub);
    float kick = micBass - micSub;
    if (kick < 0.0f) {
      kick = -kick;
    }
    if (kick > micEnv) {
      micEnv += MIC_ENV_ATTACK * (kick - micEnv);
    } else {
      micEnv += MIC_ENV_RELEASE * (kick - micEnv);
    }
  }

  if (micPrime < 60) {
    micPrime++;
    micFloor = micEnv;
    micSlow = micEnv;
    return 0;
  }

  micFloor += MIC_FLOOR_ALPHA * (micEnv - micFloor);
  micSlow += 0.02f * (micEnv - micSlow);

  if (micEnv > micSlow * MIC_PUNCH_RATIO && micEnv > micFloor + MIC_GATE) {
    micPunch = 1.0f;
  }
  micPunch *= MIC_PUNCH_DECAY;

  const float thresh = micFloor * 1.25f + MIC_GATE;
  const float range = micFloor * 3.5f + 35.0f;
  float follow = (micEnv - thresh) / range;
  if (follow < 0.0f) {
    follow = 0.0f;
  }
  if (follow > 1.0f) {
    follow = 1.0f;
  }

  float out = follow;
  if (micPunch > out) {
    out = micPunch;
  }
  if (out < 0.04f) {
    out = 0.0f;
  }
  return (uint8_t)(out * 255.0f + 0.5f);
}

// --------------------------------------------------------------------------
// Per-pixel patterns
//
// All of these are pure functions of gPhase and gCount — nothing accumulates
// between frames. That keeps them identical whether the loop is running at
// 500 Hz with the lights idle or crawling while BLE reconnects, and it is why
// the speed control can be a plain multiplier on the clock.
// --------------------------------------------------------------------------

void tint(uint16_t i, uint8_t level) {
  CRGB c = gColor;
  c.nscale8_video(level);
  gPixels[i] = c;
}

void renderComet() {
  const uint16_t span = gCount * kSub;
  const uint16_t head = (uint16_t)((gPhase / 4u) % span);
  const uint16_t tail = 10u * kSub;
  for (uint16_t i = 0; i < gCount; i++) {
    const uint16_t d = (uint16_t)((head + span - i * kSub) % span);
    tint(i, d < tail ? (uint8_t)(255u - (d * 255u) / tail) : 0);
  }
}

void renderScanner() {
  const uint16_t span = gCount * kSub;
  const uint16_t period = (uint16_t)(span * 2u);
  const uint16_t t = (uint16_t)((gPhase / 4u) % period);
  const uint16_t head = t < span ? t : (uint16_t)(period - 1u - t);
  const uint16_t tail = 4u * kSub;
  for (uint16_t i = 0; i < gCount; i++) {
    const uint16_t p = (uint16_t)(i * kSub);
    const uint16_t d = p > head ? (uint16_t)(p - head) : (uint16_t)(head - p);
    tint(i, d < tail ? (uint8_t)(255u - (d * 255u) / tail) : 0);
  }
}

void renderTwinkle() {
  for (uint16_t i = 0; i < gCount; i++) {
    const uint8_t seed = hash8(i);
    const uint8_t rate = (uint8_t)(6u + (seed & 7u));
    // Reduced mod 96*256 first so the uint8 wrap stays continuous forever
    // instead of jumping once every few days.
    const uint8_t t = (uint8_t)((((gPhase % 24576u) * rate) / 96u) + seed);
    const uint8_t w = quadwave8(t);
    // Only the crest of each pixel's wave lights. Most of the strip is dark at
    // any instant, which is what makes them read as sparks and not a ripple.
    tint(i, w < 160 ? 0 : (uint8_t)(((uint16_t)(w - 160) * 255u) / 95u));
  }
}

void renderFire() {
  for (uint16_t i = 0; i < gCount; i++) {
    // inoise8's lattice wraps at 65536, so the rolling y coordinate can run off
    // the end of a uint16 without a seam.
    const uint8_t n = inoise8((uint16_t)(i * 70u), (uint16_t)(gPhase / 2u));
    const uint8_t cool = (uint8_t)(((uint32_t)i * 200u) / gCount);
    gPixels[i] = HeatColor(qsub8(scale8(n, 220), cool));
  }
}

void renderRainbow() {
  const uint8_t delta = gCount > 1 ? (uint8_t)(255u / gCount) : 0;
  fill_rainbow(gPixels, (int)gCount, (uint8_t)((gPhase / 40u) & 0xFFu),
               delta > 0 ? delta : 1);
}

void renderWipe() {
  const uint16_t span = gCount * kSub;
  const uint16_t t = (uint16_t)((gPhase / 6u) % (uint16_t)(span * 2u));
  const bool filling = t < span;
  const uint16_t edge = filling ? t : (uint16_t)(t - span);
  for (uint16_t i = 0; i < gCount; i++) {
    const bool below = (uint16_t)(i * kSub) < edge;
    gPixels[i] = (below == filling) ? gColor : CRGB::Black;
  }
}

void renderChase() {
  const uint8_t step = (uint8_t)((gPhase / 110u) % 3u);
  for (uint16_t i = 0; i < gCount; i++) {
    gPixels[i] = ((i + step) % 3u == 0u) ? gColor : CRGB::Black;
  }
}

void renderStrip(uint8_t level) {
  if (!gOn || level == 0) {
    fill_solid(gPixels, LED_COUNT_MAX, CRGB::Black);
    lastDuty = 0;
    return;
  }

  lastDuty = scale8(level, gBrightness);

  switch (gMode) {
    case MODE_PARTY:
      fill_rainbow(gPixels, (int)gCount, (uint8_t)((gPhase / 12u) & 0xFFu), 7);
      break;
    case MODE_BASS: {
      CRGB c = CHSV(192, 255, 255);
      if (level > 210) {
        c = CRGB::White;
      }
      fill_solid(gPixels, gCount, c);
      nscale8(gPixels, gCount, level);
      const uint16_t lit = ((uint16_t)level * gCount) / 255u;
      for (uint16_t i = lit; i < gCount; i++) {
        gPixels[i].nscale8_video(40);
      }
      break;
    }
    case MODE_COMET:
      renderComet();
      break;
    case MODE_SCANNER:
      renderScanner();
      break;
    case MODE_TWINKLE:
      renderTwinkle();
      break;
    case MODE_FIRE:
      renderFire();
      break;
    case MODE_RAINBOW:
      renderRainbow();
      break;
    case MODE_WIPE:
      renderWipe();
      break;
    case MODE_CHASE:
      renderChase();
      break;
    default: {
      CRGB c = gColor;
      c.nscale8_video(level);
      fill_solid(gPixels, gCount, c);
      break;
    }
  }

  // Pixels past the configured length are not wired up. They are not clocked
  // out either, but keeping them black means shortening the strip can never
  // leave a stale tail lit.
  if (gCount < LED_COUNT_MAX) {
    fill_solid(gPixels + gCount, LED_COUNT_MAX - gCount, CRGB::Black);
  }
}

void writeWire() {
  const uint8_t *map = kOrderMap[gOrder];
  for (uint16_t i = 0; i < gCount; i++) {
    const uint8_t *src = gPixels[i].raw;
    uint8_t *dst = leds[i].raw;
    dst[0] = src[map[0]];
    dst[1] = src[map[1]];
    dst[2] = src[map[2]];
  }
}

}  // namespace

void begin() {
  pinMode(PIN_MIC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MIC, ADC_11db);
  analogRead(PIN_MIC);

  // Registered as RGB — the real order is applied by writeWire() so that it can
  // change without a rebuild.
  FastLED.addLeds<WS2812B, PIN_LED_DATA, RGB>(leds, LED_COUNT_MAX);
  gStarted = true;
  setCount(gCount);
  setMaxMilliamps(gMaxMa);
  FastLED.setBrightness(gBrightness);
  fill_solid(gPixels, LED_COUNT_MAX, CRGB::Black);
  fill_solid(leds, LED_COUNT_MAX, CRGB::Black);
  FastLED.show();
  resetMic();
  gPhaseAt = millis();
}

void loop() {
  advancePhase(millis());

  uint8_t level = 0;
  if (gOn) {
    if (gMode == MODE_BASS) {
      level = bassDuty();
    } else {
      level = patternDuty(gPhase);
    }
  }
  FastLED.setBrightness(gBrightness);
  renderStrip(level);
  writeWire();
  FastLED.show();
}

void setMode(uint8_t mode) {
  if (mode < MODE_COUNT) {
    if (gMode != mode && mode == MODE_BASS) {
      resetMic();
    }
    gMode = mode;
    gOn = true;
  }
}

void setBrightness(uint8_t brightness) {
  if (brightness < BRIGHTNESS_MIN) {
    brightness = BRIGHTNESS_MIN;
  }
  gBrightness = brightness;
}

void setColor(uint8_t r, uint8_t g, uint8_t b) { gColor = CRGB(r, g, b); }

void setSpeed(uint8_t speed) {
  if (speed < SPEED_MIN) {
    speed = SPEED_MIN;
  }
  gSpeed = speed;
}

void setOrder(uint8_t order) {
  if (order < ORDER_COUNT) {
    gOrder = order;
  }
}

void setCount(uint16_t count) {
  if (count < 1) {
    count = 1;
  }
  if (count > LED_COUNT_MAX) {
    count = LED_COUNT_MAX;
  }
  gCount = count;
  if (gStarted) {
    // Keeps FastLED's power estimate and frame time honest about how much
    // strip is actually attached.
    FastLED[0].setLeds(leds, (int)gCount);
  }
}

void setMaxMilliamps(uint16_t milliamps) {
  if (milliamps < LED_MAX_MA_MIN) {
    milliamps = LED_MAX_MA_MIN;
  }
  if (milliamps > LED_MAX_MA_MAX) {
    milliamps = LED_MAX_MA_MAX;
  }
  gMaxMa = milliamps;
  FastLED.setMaxPowerInVoltsAndMilliamps(5, gMaxMa);
}

void setOn(bool on) { gOn = on; }

void toggle() { gOn = !gOn; }

void nudgeBrightness(int delta) {
  int next = (int)gBrightness + delta;
  if (next < BRIGHTNESS_MIN) {
    next = BRIGHTNESS_MIN;
  }
  if (next > 255) {
    next = 255;
  }
  gBrightness = (uint8_t)next;
}

uint8_t mode() { return gMode; }
uint8_t brightness() { return gBrightness; }
bool on() { return gOn; }
uint8_t speed() { return gSpeed; }
uint8_t order() { return gOrder; }
uint16_t count() { return gCount; }
uint16_t maxMilliamps() { return gMaxMa; }
const char *modeName() { return kNames[gMode]; }
const char *modeNameAt(uint8_t index) {
  return index < MODE_COUNT ? kNames[index] : "";
}
const char *orderName() { return kOrderNames[gOrder]; }
const char *orderNameAt(uint8_t index) {
  return index < ORDER_COUNT ? kOrderNames[index] : "";
}
uint8_t brightnessPercent() { return (uint8_t)((gBrightness * 100u) / 255u); }
uint8_t speedPercent() { return (uint8_t)(((uint16_t)gSpeed * 100u) / SPEED_NORMAL); }
uint8_t outputLevel() { return lastDuty; }

void color(uint8_t *r, uint8_t *g, uint8_t *b) {
  *r = gColor.r;
  *g = gColor.g;
  *b = gColor.b;
}

}  // namespace Leds
