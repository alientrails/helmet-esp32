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
uint8_t lastSpeed = 0xFF;
uint8_t lastOrder = 0xFF;
uint16_t lastCount = 0xFFFF;
uint16_t lastMaxMa = 0xFFFF;
uint16_t lastSkip = 0xFFFF;

void rememberCurrent() {
  lastMode = Leds::mode();
  lastBri = Leds::brightness();
  lastOn = Leds::on();
  Leds::color(&lastR, &lastG, &lastB);
  lastSpeed = Leds::speed();
  lastOrder = Leds::order();
  lastCount = Leds::count();
  lastMaxMa = Leds::maxMilliamps();
  lastSkip = Leds::skip();
}

bool changed() {
  uint8_t r, g, b;
  Leds::color(&r, &g, &b);
  return Leds::mode() != lastMode || Leds::brightness() != lastBri ||
         Leds::on() != lastOn || r != lastR || g != lastG || b != lastB ||
         Leds::speed() != lastSpeed || Leds::order() != lastOrder ||
         Leds::count() != lastCount || Leds::maxMilliamps() != lastMaxMa ||
         Leds::skip() != lastSkip;
}

}  // namespace

void begin() {
  prefs.begin(PREFS_NAMESPACE, false);

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

  rememberCurrent();
}

void markDirty() {
  dirty = true;
  dirtyAt = millis();
}

void loop() {
  if (changed()) {
    markDirty();
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
  prefs.putUChar("speed", lastSpeed);
  prefs.putUChar("order", lastOrder);
  prefs.putUShort("count", lastCount);
  prefs.putUShort("maxma", lastMaxMa);
  prefs.putUShort("skip", lastSkip);
  dirty = false;
}

}  // namespace Persist
