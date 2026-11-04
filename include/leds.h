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
  // Everything below was added later and is deliberately appended rather than
  // slotted in alphabetically: the index is what goes over BLE and into flash,
  // so inserting in the middle would silently change the mode a helmet comes
  // back up in after an update.
  MODE_COMET,
  MODE_SCANNER,
  MODE_TWINKLE,
  MODE_FIRE,
  MODE_RAINBOW,
  MODE_WIPE,
  MODE_CHASE,
  MODE_COUNT
};

// Wire order of the strip. WS2812B reels are almost always GRB, but RGB and
// BGR clones turn up often enough that guessing wrong once costs an evening.
// Applied in software on the way to the strip, so it can be changed live.
enum LedOrder : uint8_t {
  ORDER_GRB = 0,
  ORDER_RGB,
  ORDER_BRG,
  ORDER_RBG,
  ORDER_GBR,
  ORDER_BGR,
  ORDER_COUNT
};

namespace Leds {
void begin();
void loop();

void setMode(uint8_t mode);
void setBrightness(uint8_t brightness);
void setOn(bool on);
void toggle();
void nudgeBrightness(int delta);
void setColor(uint8_t r, uint8_t g, uint8_t b);  // tint used by solid-color modes

// Animation rate. SPEED_NORMAL is 1x; the patterns run off a scaled clock, so
// this only stretches time and never changes what a pattern looks like.
void setSpeed(uint8_t speed);

// Strip wiring, adjustable at runtime so a new reel can be set up from the
// touchscreen instead of a rebuild.
void setOrder(uint8_t order);
void setCount(uint16_t count);
void setMaxMilliamps(uint16_t milliamps);

// Pixels at the head of the strip to leave dark because they are buried in the
// shell. Patterns are drawn across the lit() pixels that follow, so raising
// this shortens the canvas rather than blanking part of the picture. Clamped
// to leave at least one pixel lit.
void setSkip(uint16_t skip);

uint8_t mode();
uint8_t brightness();
bool on();
uint8_t speed();
uint8_t order();
uint16_t count();      // pixels wired to the strip, hidden ones included
uint16_t maxMilliamps();
uint16_t skip();       // hidden pixels at the head
uint16_t lit();        // count() - skip(): the canvas a pattern is drawn on
const char *modeName();
const char *modeNameAt(uint8_t index);
const char *orderName();
const char *orderNameAt(uint8_t index);
uint8_t brightnessPercent();
uint8_t speedPercent();  // 100 = 1x, so the UI can show "100%" at the default
uint8_t outputLevel();   // live PWM 0–255, for the OLED meter
void color(uint8_t *r, uint8_t *g, uint8_t *b);
}  // namespace Leds
