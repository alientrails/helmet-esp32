#include "leds.h"

#include <math.h>

#include "config.h"

namespace Leds {
namespace {

uint8_t gMode = MODE_SOLID;
uint8_t gBrightness = BRIGHTNESS_DEFAULT;
bool gOn = true;
uint8_t lastDuty = 255;  // force first write
uint32_t partyRng = 0xC0FFEEu;
uint32_t partyUntil = 0;
bool partyLit = false;

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

uint32_t partyRand() {
  partyRng = partyRng * 1664525u + 1013904223u;
  return partyRng;
}

void pwmWriteRaw(uint8_t duty) {
  if (duty == lastDuty) {
    return;
  }
  lastDuty = duty;
  ledcWrite(PWM_CHANNEL, duty);
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
    case MODE_PARTY:
      if (now >= partyUntil) {
        partyLit = !partyLit;
        const uint32_t span =
            partyLit ? (40u + partyRand() % 120u) : (60u + partyRand() % 420u);
        partyUntil = now + span;
      }
      return partyLit ? 255 : 0;
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

}  // namespace

void begin() {
  pinMode(PIN_MIC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MIC, ADC_11db);
  analogRead(PIN_MIC);

  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(PIN_LED_PWM, PWM_CHANNEL);
  lastDuty = 255;
  pwmWriteRaw(0);
  resetMic();
}

void loop() {
  uint8_t duty = 0;
  if (gOn) {
    if (gMode == MODE_BASS) {
      duty = (uint8_t)((bassDuty() * (uint16_t)gBrightness) / 255u);
    } else {
      const uint16_t raw = patternDuty(millis());
      duty = (uint8_t)((raw * (uint16_t)gBrightness) / 255u);
    }
  }
  pwmWriteRaw(duty);
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

}  // namespace Leds
