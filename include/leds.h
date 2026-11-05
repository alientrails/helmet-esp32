#pragma once

#include <Arduino.h>

#include "bass_dsp.h"

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

// How Bass mode draws its output on the strip.
enum BassStyle : uint8_t {
  BASS_STYLE_METER = 0,  // a bar that grows along the strip, on a dim ground
  BASS_STYLE_PULSE,      // the whole strip breathes with the beat
  BASS_STYLE_CENTRE,     // grows out from the middle in both directions
  BASS_STYLE_COUNT
};

// Bass look flags, OR-ed together.
constexpr uint8_t BASS_FLAG_FLASH = 0x01;      // white flash on the loudest peaks
constexpr uint8_t BASS_FLAG_MY_COLOUR = 0x02;  // the chosen colour, not violet
constexpr uint8_t BASS_FLAG_RAINBOW = 0x04;    // a rainbow across the strip
constexpr uint8_t BASS_FLAGS_DEFAULT = BASS_FLAG_FLASH;

// The colour is one choice of three held in two bits, so a combination the UI
// never sends still has to mean something: rainbow wins, then my colour, then
// violet. Use these rather than testing the bits by hand, so the firmware, the
// touchscreen and the app all resolve an odd pair the same way. The app mirrors
// them in HelmetProtocol.bassColour()/withBassColour().
enum BassColour : uint8_t {
  BASS_COLOUR_VIOLET = 0,
  BASS_COLOUR_MINE,
  BASS_COLOUR_RAINBOW,
  BASS_COLOUR_COUNT
};

constexpr uint8_t bassColour(uint8_t flags) {
  return (flags & BASS_FLAG_RAINBOW)     ? BASS_COLOUR_RAINBOW
         : (flags & BASS_FLAG_MY_COLOUR) ? BASS_COLOUR_MINE
                                         : BASS_COLOUR_VIOLET;
}

// [flags] with the colour bits replaced by [choice], leaving BASS_FLAG_FLASH
// alone. Exactly one colour bit is ever set this way. One return statement and
// no locals: the firmware builds as C++11, where that is all a constexpr
// function may be.
constexpr uint8_t withBassColour(uint8_t flags, uint8_t choice) {
  return (uint8_t)((flags & (uint8_t)~(BASS_FLAG_MY_COLOUR | BASS_FLAG_RAINBOW)) |
                   (choice == BASS_COLOUR_MINE      ? BASS_FLAG_MY_COLOUR
                    : choice == BASS_COLOUR_RAINBOW ? BASS_FLAG_RAINBOW
                                                    : 0));
}

// Every Bass setting, by the id that goes over BLE (SET_BASS) and into flash.
// Append only, like HelmetMode.
enum BassParam : uint8_t {
  BASS_SENSITIVITY = 0,  // 0..255
  BASS_GATE,             // meter units, see BassDsp::kMeterDiv
  BASS_KICK,             // 0..255
  BASS_DECAY,            // 0..255
  BASS_BAND,             // BassDsp::Band
  BASS_STYLE,            // BassStyle
  BASS_FLAGS,            // BASS_FLAG_*
  BASS_HUM,              // 0 off, 1 notch out mains hum
  BASS_PARAM_COUNT
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

// Bass tuning and look, by BassParam id. Values out of range are clamped.
// Safe to call from any task: the detector runs on its own and picks changes
// up on its next sample.
void setBass(uint8_t param, uint8_t value);
uint8_t bass(uint8_t param);

// Live detector reading for the meters. Only moving while bassLive().
BassDsp::Meter bassMeter();
bool bassLive();  // the 1 kHz sampler is running, i.e. the mode is Bass

// Telemetry for the serial console. bassCostUs() is how long one sample's
// burst of reads took. bassCapture() records the next n samples into buf from
// the sampler task; poll bassCaptureDone() before reading it.
uint16_t bassCostUs();
void bassCapture(uint16_t *buf, uint16_t n);
bool bassCaptureDone();

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
