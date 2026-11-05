#include "persist.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "leds.h"

namespace Persist {
namespace {

Preferences prefs;

// Writes wait until the settings have held still this long, so dragging a
// slider is one flash write after you let go rather than one per step.
constexpr uint32_t kSettleMs = 600;

// NVS keys for the Bass settings, indexed by BassParam. Append only, in step
// with the enum.
const char *const kBassKeys[BASS_PARAM_COUNT] = {"bsens",  "bgate",  "bkick",
                                                 "bdecay", "bband",  "bstyle",
                                                 "bflags", "bhum"};

// Every saved setting packed into one array, so "did anything change" and
// "is this what is in flash" are each a single memcmp.
//
// The previous version compared live values against the last *saved* ones and
// restarted the settle timer on every pass while they differed -- which is
// every pass until a save, so the timer never ran out and nothing was ever
// written. Keeping what was last *seen* separate from what was last *saved*
// is the fix: the timer restarts only when the settings actually move.
enum Slot : uint8_t {
  S_MODE = 0,
  S_BRI,
  S_ON,
  S_R,
  S_G,
  S_B,
  S_SPEED,
  S_ORDER,
  S_COUNT_LO,
  S_COUNT_HI,
  S_MAXMA_LO,
  S_MAXMA_HI,
  S_SKIP_LO,
  S_SKIP_HI,
  S_BASS,  // BASS_PARAM_COUNT bytes from here
  S_LEN = S_BASS + BASS_PARAM_COUNT
};

uint8_t seen[S_LEN];   // the settings as the loop last saw them
uint8_t saved[S_LEN];  // the settings as they are in flash
uint32_t seenAt = 0;   // when `seen` last changed

void snapshot(uint8_t *s) {
  s[S_MODE] = Leds::mode();
  s[S_BRI] = Leds::brightness();
  s[S_ON] = Leds::on() ? 1 : 0;
  Leds::color(&s[S_R], &s[S_G], &s[S_B]);
  s[S_SPEED] = Leds::speed();
  s[S_ORDER] = Leds::order();
  const uint16_t count = Leds::count();
  s[S_COUNT_LO] = (uint8_t)count;
  s[S_COUNT_HI] = (uint8_t)(count >> 8);
  const uint16_t maxMa = Leds::maxMilliamps();
  s[S_MAXMA_LO] = (uint8_t)maxMa;
  s[S_MAXMA_HI] = (uint8_t)(maxMa >> 8);
  const uint16_t skip = Leds::skip();
  s[S_SKIP_LO] = (uint8_t)skip;
  s[S_SKIP_HI] = (uint8_t)(skip >> 8);
  for (uint8_t i = 0; i < BASS_PARAM_COUNT; i++) {
    s[S_BASS + i] = Leds::bass(i);
  }
}

uint16_t u16(const uint8_t *s, uint8_t lo) { return (uint16_t)(s[lo] | (s[lo + 1] << 8)); }

// Writes exactly the snapshot it is given, so `saved` can never disagree with
// what reached flash. NVS skips a write whose value is unchanged, so putting
// every key costs nothing for the ones that did not move.
void write(const uint8_t *s) {
  prefs.putUChar("mode", s[S_MODE]);
  prefs.putUChar("bri", s[S_BRI]);
  prefs.putBool("on", s[S_ON] != 0);
  prefs.putUChar("r", s[S_R]);
  prefs.putUChar("g", s[S_G]);
  prefs.putUChar("b", s[S_B]);
  prefs.putUChar("speed", s[S_SPEED]);
  prefs.putUChar("order", s[S_ORDER]);
  prefs.putUShort("count", u16(s, S_COUNT_LO));
  prefs.putUShort("maxma", u16(s, S_MAXMA_LO));
  prefs.putUShort("skip", u16(s, S_SKIP_LO));
  for (uint8_t i = 0; i < BASS_PARAM_COUNT; i++) {
    prefs.putUChar(kBassKeys[i], s[S_BASS + i]);
  }
}

}  // namespace

void begin() {
  prefs.begin(PREFS_NAMESPACE, false);

  // setMode() switches the lights on, so on/off is restored after it.
  Leds::setMode(prefs.getUChar("mode", MODE_SOLID));
  Leds::setBrightness(prefs.getUChar("bri", BRIGHTNESS_DEFAULT));
  Leds::setOn(prefs.getBool("on", true));
  Leds::setColor(prefs.getUChar("r", 255), prefs.getUChar("g", 255),
                 prefs.getUChar("b", 255));
  Leds::setSpeed(prefs.getUChar("speed", SPEED_DEFAULT));
  Leds::setOrder(prefs.getUChar("order", LED_ORDER_DEFAULT));
  Leds::setCount(prefs.getUShort("count", LED_COUNT));
  Leds::setMaxMilliamps(prefs.getUShort("maxma", LED_MAX_MA));
  // After setCount: the skip is clamped against the strip length.
  Leds::setSkip(prefs.getUShort("skip", LED_SKIP));
  // A fresh helmet starts on the firmware's defaults; reading them back first
  // means a missing key falls back to that, not to zero.
  for (uint8_t i = 0; i < BASS_PARAM_COUNT; i++) {
    Leds::setBass(i, prefs.getUChar(kBassKeys[i], Leds::bass(i)));
  }

  // What was just loaded is, by definition, what is in flash.
  snapshot(seen);
  memcpy(saved, seen, S_LEN);
  seenAt = millis();
}

void markDirty() {
  // Changes are picked up by comparison in loop(), so all an explicit nudge
  // needs to do is restart the settle time.
  seenAt = millis();
}

void loop() {
  uint8_t now[S_LEN];
  snapshot(now);
  if (memcmp(now, seen, S_LEN) != 0) {
    memcpy(seen, now, S_LEN);
    seenAt = millis();
  }
  if (memcmp(seen, saved, S_LEN) == 0) {
    return;  // nothing unsaved
  }
  if (millis() - seenAt < kSettleMs) {
    return;  // still being adjusted
  }
  write(seen);
  memcpy(saved, seen, S_LEN);
}

}  // namespace Persist
