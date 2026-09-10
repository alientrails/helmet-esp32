#include "persist.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "leds.h"

namespace Persist {
namespace {

Preferences prefs;
bool dirty = false;
uint32_t dirtyAt = 0;

uint8_t lastMode = 0xFF;
uint8_t lastBri = 0xFF;
bool lastOn = true;
uint8_t lastR = 0xFF;
uint8_t lastG = 0xFF;
uint8_t lastB = 0xFF;

void rememberCurrent() {
  lastMode = Leds::mode();
  lastBri = Leds::brightness();
  lastOn = Leds::on();
  Leds::color(&lastR, &lastG, &lastB);
}

}  // namespace

void begin() {
  prefs.begin(PREFS_NAMESPACE, false);

  Leds::setMode(prefs.getUChar("mode", MODE_SOLID));
  Leds::setBrightness(prefs.getUChar("bri", BRIGHTNESS_DEFAULT));
  Leds::setOn(prefs.getBool("on", true));
  Leds::setColor(prefs.getUChar("r", 255), prefs.getUChar("g", 255),
                 prefs.getUChar("b", 255));

  rememberCurrent();
}

void markDirty() {
  dirty = true;
  dirtyAt = millis();
}

void loop() {
  if (Leds::mode() != lastMode || Leds::brightness() != lastBri ||
      Leds::on() != lastOn) {
    markDirty();
  } else {
    uint8_t r, g, b;
    Leds::color(&r, &g, &b);
    if (r != lastR || g != lastG || b != lastB) {
      markDirty();
    }
  }

  if (!dirty || (millis() - dirtyAt) < 600) {
    return;
  }

  rememberCurrent();
  prefs.putUChar("mode", lastMode);
  prefs.putUChar("bri", lastBri);
  prefs.putBool("on", lastOn);
  prefs.putUChar("r", lastR);
  prefs.putUChar("g", lastG);
  prefs.putUChar("b", lastB);
  dirty = false;
}

}  // namespace Persist
