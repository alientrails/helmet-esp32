#pragma once

#include <Arduino.h>

enum HelmetMode : uint8_t {
  MODE_SOLID = 0,
  MODE_BREATHE,
  MODE_HEARTBEAT,
  MODE_BLINK_SLOW,
  MODE_BLINK_FAST,
  MODE_STROBE,
  MODE_SOS,
  MODE_PARTY,
  MODE_BASS,
  MODE_COUNT
};

namespace Leds {
void begin();
void loop();

void setMode(uint8_t mode);
void setBrightness(uint8_t brightness);
void setOn(bool on);
void toggle();
void nudgeBrightness(int delta);

uint8_t mode();
uint8_t brightness();
bool on();
const char *modeName();
const char *modeNameAt(uint8_t index);
uint8_t brightnessPercent();
uint8_t outputLevel();  // live PWM 0–255, for the OLED meter
}  // namespace Leds
