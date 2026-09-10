#include "leds.h"

#include <FastLED.h>
#include <math.h>

#include "config.h"

namespace Leds {
namespace {

CRGB leds[LED_COUNT];

uint8_t gMode = MODE_SOLID;
uint8_t gBrightness = BRIGHTNESS_DEFAULT;
bool gOn = true;
uint8_t lastDuty = 0;
CRGB gColor = CRGB::White;

float micDc = 2048.0f;
float micBass = 0.0f;
float micSub = 0.0f;
float micEnv = 0.0f;
float micFloor = 40.0f;
float micSlow = 40.0f;
float micPunch = 0.0f;
uint16_t micPrime = 0;

const char *kNames[MODE_COUNT] = {"Solid",      "Breathe", "Heartbeat", "Slow blink",
                                  "Fast blink", "Strobe",  "SOS",       "Party",
                                  "Bass"};

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
    case MODE_PARTY:
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

void renderStrip(uint8_t level) {
  if (!gOn || level == 0) {
    fill_solid(leds, LED_COUNT, CRGB::Black);
    lastDuty = 0;
    return;
  }

  lastDuty = scale8(level, gBrightness);

  switch (gMode) {
    case MODE_PARTY:
      fill_rainbow(leds, LED_COUNT, (millis() / 12) & 0xFF, 7);
      break;
    case MODE_BASS: {
      CRGB c = CHSV(192, 255, 255);
      if (level > 210) {
        c = CRGB::White;
      }
      fill_solid(leds, LED_COUNT, c);
      nscale8(leds, LED_COUNT, level);
      const uint16_t lit = ((uint16_t)level * LED_COUNT) / 255u;
      for (uint16_t i = lit; i < LED_COUNT; i++) {
        leds[i].nscale8_video(40);
      }
      break;
    }
    default: {
      CRGB c = gColor;
      c.nscale8_video(level);
      fill_solid(leds, LED_COUNT, c);
      break;
    }
  }
}

}  // namespace

void begin() {
  pinMode(PIN_MIC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MIC, ADC_11db);
  analogRead(PIN_MIC);

  FastLED.addLeds<WS2812B, PIN_LED_DATA, GRB>(leds, LED_COUNT);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_MAX_MA);
  FastLED.setBrightness(gBrightness);
  fill_solid(leds, LED_COUNT, CRGB::Black);
  FastLED.show();
  resetMic();
}

void loop() {
  uint8_t level = 0;
  if (gOn) {
    if (gMode == MODE_BASS) {
      level = bassDuty();
    } else {
      level = patternDuty(millis());
    }
  }
  FastLED.setBrightness(gBrightness);
  renderStrip(level);
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
const char *modeName() { return kNames[gMode]; }
const char *modeNameAt(uint8_t index) {
  return index < MODE_COUNT ? kNames[index] : "";
}
uint8_t brightnessPercent() { return (uint8_t)((gBrightness * 100u) / 255u); }
uint8_t outputLevel() { return lastDuty; }

void color(uint8_t *r, uint8_t *g, uint8_t *b) {
  *r = gColor.r;
  *g = gColor.g;
  *b = gColor.b;
}

}  // namespace Leds
