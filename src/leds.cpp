#include "leds.h"

#include <FastLED.h>
#include <driver/adc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "bass_dsp.h"
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

// The head of the strip disappears inside the shell, so those pixels are never
// painted by a pattern. gLit is the canvas that is left -- held in a variable
// rather than subtracted in every per-pixel loop, and kept in step with gCount
// and gSkip by relit().
uint16_t gSkip = LED_SKIP;
uint16_t gLit = LED_COUNT - LED_SKIP;
bool gStarted = false;

// Scaled animation clock, in milliseconds-at-1x. Every pattern is a pure
// function of this, so changing the speed stretches time smoothly instead of
// jumping the animation, and a slow frame never shortens a blink.
uint32_t gPhase = 0;
uint32_t gPhaseAt = 0;
uint16_t gPhaseRem = 0;  // leftover 1/SPEED_NORMAL ticks, carried between frames

// ---- Bass. The detector runs in its own task at a steady 1 kHz (see
// bassTask()); everything here is the handoff between it and the rest of the
// firmware, which sets parameters from the UI and BLE tasks and reads the
// output from the render loop.
BassDsp::Detector gBass;
BassDsp::Params gBassParams;  // what the UI/BLE side last set
volatile uint8_t gBassSeq = 0;  // bumped on every change, so the task copies once
portMUX_TYPE gBassMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool gBassActive = false;
volatile bool gBassResetPending = false;
volatile uint16_t gBassCostUs = 0;
uint8_t gBassStyle = BASS_STYLE_METER;
uint8_t gBassFlags = BASS_FLAGS_DEFAULT;
TaskHandle_t gBassTask = nullptr;

// Raw capture for the serial console: the task fills it, the console prints.
uint16_t *volatile gCapBuf = nullptr;
volatile uint16_t gCapLen = 0;
volatile uint16_t gCapPos = 0;

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

// Runs the detector at exactly 1 kHz, whatever the render loop is doing. The
// loop spends anything from 2 ms to 30 ms a frame depending on what LVGL has
// to repaint, so a detector stepped from there cannot have meaningful filter
// corners or decay times -- which is the main reason the old one flickered.
void bassTask(void *) {
  TickType_t last = xTaskGetTickCount();
  uint8_t seenSeq = (uint8_t)(gBassSeq - 1);
  uint16_t burst[MIC_BURST];

  // analogRead() re-runs pinMode() and reprograms the channel's attenuation on
  // every call before it converts -- about 85 us a read on the CYD, which at
  // six reads a millisecond was half a core. The pin is configured once by
  // begin(), so ADC1 is read directly here, which returns the same raw value
  // without the setup. ADC2 pins keep the slow, portable path.
  const int8_t ch = digitalPinToAnalogChannel(PIN_MIC);
  const bool onAdc1 = ch >= 0 && ch < SOC_ADC_MAX_CHANNEL_NUM;
  for (;;) {
    if (!gBassActive) {
      // Not in Bass mode: no sampling at all, so it costs nothing.
      vTaskDelay(pdMS_TO_TICKS(20));
      last = xTaskGetTickCount();
      continue;
    }
    xTaskDelayUntil(&last, 1);

    if (gBassSeq != seenSeq) {
      BassDsp::Params p;
      portENTER_CRITICAL(&gBassMux);
      p = gBassParams;
      seenSeq = gBassSeq;
      portEXIT_CRITICAL(&gBassMux);
      gBass.setParams(p);
    }
    if (gBassResetPending) {
      gBassResetPending = false;
      gBass.reset();
    }

    const uint32_t t0 = micros();
    for (uint8_t i = 0; i < MIC_BURST; i++) {
      if (onAdc1) {
        const int v = adc1_get_raw((adc1_channel_t)ch);
        burst[i] = v < 0 ? 0 : (uint16_t)v;
      } else {
        burst[i] = (uint16_t)analogRead(PIN_MIC);
      }
    }
    const uint16_t x = BassDsp::trimmedMean(burst, MIC_BURST);
    gBassCostUs = (uint16_t)(micros() - t0);

    gBass.process(x);

    uint16_t *cap = gCapBuf;
    if (cap != nullptr && gCapPos < gCapLen) {
      cap[gCapPos] = x;
      gCapPos = gCapPos + 1;
    }
  }
}

uint8_t bassDuty() { return gBass.output(); }

// --------------------------------------------------------------------------
// Per-pixel patterns
//
// All of these are pure functions of gPhase and gLit — nothing accumulates
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
  const uint16_t span = gLit * kSub;
  const uint16_t head = (uint16_t)((gPhase / 4u) % span);
  const uint16_t tail = 10u * kSub;
  for (uint16_t i = 0; i < gLit; i++) {
    const uint16_t d = (uint16_t)((head + span - i * kSub) % span);
    tint(i, d < tail ? (uint8_t)(255u - (d * 255u) / tail) : 0);
  }
}

void renderScanner() {
  const uint16_t span = gLit * kSub;
  const uint16_t period = (uint16_t)(span * 2u);
  const uint16_t t = (uint16_t)((gPhase / 4u) % period);
  const uint16_t head = t < span ? t : (uint16_t)(period - 1u - t);
  const uint16_t tail = 4u * kSub;
  for (uint16_t i = 0; i < gLit; i++) {
    const uint16_t p = (uint16_t)(i * kSub);
    const uint16_t d = p > head ? (uint16_t)(p - head) : (uint16_t)(head - p);
    tint(i, d < tail ? (uint8_t)(255u - (d * 255u) / tail) : 0);
  }
}

void renderTwinkle() {
  for (uint16_t i = 0; i < gLit; i++) {
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
  for (uint16_t i = 0; i < gLit; i++) {
    // inoise8's lattice wraps at 65536, so the rolling y coordinate can run off
    // the end of a uint16 without a seam.
    const uint8_t n = inoise8((uint16_t)(i * 70u), (uint16_t)(gPhase / 2u));
    const uint8_t cool = (uint8_t)(((uint32_t)i * 200u) / gLit);
    gPixels[i] = HeatColor(qsub8(scale8(n, 220), cool));
  }
}

void renderRainbow() {
  const uint8_t delta = gLit > 1 ? (uint8_t)(255u / gLit) : 0;
  fill_rainbow(gPixels, (int)gLit, (uint8_t)((gPhase / 40u) & 0xFFu),
               delta > 0 ? delta : 1);
}

// Party flashes the rainbow rather than scrolling it: Rainbow mode next door
// is the smooth sweep, and two modes doing the same sweep at different rates
// only ever read as the same mode. The band of hues across the strip is the
// one Party always had; what changed is that it is cut into flashes and the
// spectrum jumps between them instead of drifting.
void renderParty() {
  constexpr uint32_t kPeriodMs = 240;  // at 1x, so ~4 flashes a second
  constexpr uint32_t kOnMs = 130;      // and dark for the rest of each one
  if (gPhase % kPeriodMs >= kOnMs) {
    fill_solid(gPixels, gLit, CRGB::Black);
    return;
  }
  // A big step between flashes, coprime with the 256-step hue wheel, so
  // consecutive flashes contrast instead of landing on neighbouring hues and
  // reading as a slow fade.
  const uint8_t hue = (uint8_t)(((gPhase / kPeriodMs) * 73u) & 0xFFu);
  fill_rainbow(gPixels, (int)gLit, hue, 7);
}

void renderWipe() {
  const uint16_t span = gLit * kSub;
  const uint16_t t = (uint16_t)((gPhase / 6u) % (uint16_t)(span * 2u));
  const bool filling = t < span;
  const uint16_t edge = filling ? t : (uint16_t)(t - span);
  for (uint16_t i = 0; i < gLit; i++) {
    const bool below = (uint16_t)(i * kSub) < edge;
    gPixels[i] = (below == filling) ? gColor : CRGB::Black;
  }
}

// Bass mode's picture. The detector's output arrives as level; style and
// colour are only how it is drawn, so none of them change what triggers.
void renderBass(uint8_t level) {
  // A peak flash is white whatever the colour is, so it reads as the detector
  // firing rather than as part of the picture.
  const bool flashing = (gBassFlags & BASS_FLAG_FLASH) && level > 210;
  if (!flashing && bassColour(gBassFlags) == BASS_COLOUR_RAINBOW) {
    // A full spectrum over the lit run, drifting on the scaled clock so the
    // speed slider reaches it like every other pattern. The level scaling
    // below then does the same work it does for a single colour: the meter
    // reveals more of the spectrum as it grows, and pulse breathes all of it.
    const uint8_t delta = gLit > 1 ? (uint8_t)(255u / gLit) : 0;
    fill_rainbow(gPixels, (int)gLit, (uint8_t)((gPhase / 40u) & 0xFFu),
                 delta > 0 ? delta : 1);
  } else {
    CRGB c = (bassColour(gBassFlags) == BASS_COLOUR_MINE) ? gColor : CRGB(CHSV(192, 255, 255));
    if (flashing) {
      c = CRGB::White;
    }
    fill_solid(gPixels, gLit, c);
  }
  nscale8(gPixels, gLit, level);
  if (gBassStyle == BASS_STYLE_PULSE) {
    return;  // the whole strip at the level
  }

  // Meter and Centre: a lit run on a dim ground. Pixels outside the run keep
  // a trace of the level, so the strip reads as one instrument rather than a
  // bar flickering on and off.
  if (gBassStyle == BASS_STYLE_CENTRE) {
    // Distances in half-pixels from the true centre, so an even-length strip
    // grows symmetrically instead of favouring one side by a pixel.
    const int32_t reach2 = (int32_t)(((uint32_t)level * gLit) / 255u);
    for (uint16_t i = 0; i < gLit; i++) {
      int32_t d2 = 2 * (int32_t)i - ((int32_t)gLit - 1);
      if (d2 < 0) {
        d2 = -d2;
      }
      if (d2 >= reach2) {
        gPixels[i].nscale8_video(40);
      }
    }
  } else {
    const uint16_t lit = (uint16_t)(((uint32_t)level * gLit) / 255u);
    for (uint16_t i = lit; i < gLit; i++) {
      gPixels[i].nscale8_video(40);
    }
  }
}

void renderChase() {
  const uint8_t step = (uint8_t)((gPhase / 110u) % 3u);
  for (uint16_t i = 0; i < gLit; i++) {
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
      renderParty();
      break;
    case MODE_BASS:
      renderBass(level);
      break;
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
      fill_solid(gPixels, gLit, c);
      break;
    }
  }

  // Pixels past the configured length are not wired up. They are not clocked
  // out either, but keeping them black means shortening the strip can never
  // leave a stale tail lit.
  if (gLit < LED_COUNT_MAX) {
    fill_solid(gPixels + gLit, LED_COUNT_MAX - gLit, CRGB::Black);
  }
}

// Keeps gSkip and gLit consistent with gCount. Always leaves one pixel lit:
// a fully skipped strip looks exactly like a dead one.
void relit() {
  if (gSkip > (uint16_t)(gCount - 1u)) {
    gSkip = (uint16_t)(gCount - 1u);
  }
  gLit = (uint16_t)(gCount - gSkip);
}

void writeWire() {
  const uint8_t *map = kOrderMap[gOrder];

  // The buried head is never painted, so blank it here rather than trusting it
  // to be black already -- otherwise raising the skip would strand whatever
  // those pixels were showing a frame earlier.
  if (gSkip > 0) {
    fill_solid(leds, gSkip, CRGB::Black);
  }

  // Logical pixel 0 is the first one anybody can see, so the canvas lands at
  // gSkip. gSkip + gLit == gCount, so this never runs past the strip.
  for (uint16_t i = 0; i < gLit; i++) {
    const uint8_t *src = gPixels[i].raw;
    uint8_t *dst = leds[i + gSkip].raw;
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
  // The sampler runs on the Arduino core at a priority above loop(), so a
  // heavy LVGL repaint never delays a sample. On the dual-core chips that also
  // keeps it off the core where the BLE host's bursts run.
  gBassActive = gMode == MODE_BASS;
  xTaskCreatePinnedToCore(bassTask, "bass", 3072, nullptr, 4, &gBassTask,
                          ARDUINO_RUNNING_CORE);
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
      gBassResetPending = true;  // re-prime on the bias before trusting output
    }
    gMode = mode;
    gOn = true;
    // Sampling follows the mode, not the power switch, so the meters keep
    // working with the lights off -- the quietest moment to set the gate.
    gBassActive = gMode == MODE_BASS;
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
  // A shorter strip can leave the skip longer than the strip itself.
  relit();
  if (gStarted) {
    // Keeps FastLED's power estimate and frame time honest about how much
    // strip is actually attached.
    FastLED[0].setLeds(leds, (int)gCount);
  }
}

void setSkip(uint16_t skip) {
  if (skip > LED_SKIP_MAX) {
    skip = LED_SKIP_MAX;
  }
  gSkip = skip;
  relit();
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

void setBass(uint8_t param, uint8_t value) {
  // Look settings are only read by the render loop, so no handoff needed.
  if (param == BASS_STYLE) {
    gBassStyle = value < BASS_STYLE_COUNT ? value : BASS_STYLE_METER;
    return;
  }
  if (param == BASS_FLAGS) {
    gBassFlags = value & (BASS_FLAG_FLASH | BASS_FLAG_MY_COLOUR | BASS_FLAG_RAINBOW);
    return;
  }
  portENTER_CRITICAL(&gBassMux);
  switch (param) {
    case BASS_SENSITIVITY:
      gBassParams.sensitivity = value;
      break;
    case BASS_GATE:
      gBassParams.gate = value;
      break;
    case BASS_KICK:
      gBassParams.kick = value;
      break;
    case BASS_DECAY:
      gBassParams.decay = value;
      break;
    case BASS_BAND:
      gBassParams.band = value < BassDsp::BAND_COUNT ? value : BassDsp::BAND_KICK;
      break;
    case BASS_HUM:
      gBassParams.hum = value != 0 ? 1 : 0;
      break;
    default:
      break;
  }
  gBassSeq = gBassSeq + 1;
  portEXIT_CRITICAL(&gBassMux);
}

uint8_t bass(uint8_t param) {
  switch (param) {
    case BASS_SENSITIVITY:
      return gBassParams.sensitivity;
    case BASS_GATE:
      return gBassParams.gate;
    case BASS_KICK:
      return gBassParams.kick;
    case BASS_DECAY:
      return gBassParams.decay;
    case BASS_BAND:
      return gBassParams.band;
    case BASS_STYLE:
      return gBassStyle;
    case BASS_FLAGS:
      return gBassFlags;
    case BASS_HUM:
      return gBassParams.hum;
    default:
      return 0;
  }
}

BassDsp::Meter bassMeter() { return gBass.meter(); }
bool bassLive() { return gBassActive; }
uint16_t bassCostUs() { return gBassCostUs; }

void bassCapture(uint16_t *buf, uint16_t n) {
  gCapBuf = nullptr;  // stop any capture in flight before moving the buffer
  gCapPos = 0;
  gCapLen = n;
  gCapBuf = buf;
}

bool bassCaptureDone() { return gCapBuf == nullptr || gCapPos >= gCapLen; }

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
uint16_t skip() { return gSkip; }
uint16_t lit() { return gLit; }
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
