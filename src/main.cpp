#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "leds.h"
#include "ui.h"

static Preferences prefs;
static bool prefsDirty = false;
static uint32_t prefsDirtyAt = 0;
static uint8_t lastSavedMode = 0xFF;
static uint8_t lastSavedBri = 0xFF;
static bool lastSavedOn = true;

enum BtnEvent : uint8_t { BTN_NONE, BTN_CLICK, BTN_LONG, BTN_REPEAT };

struct Button {
  int pin;
  bool lastRaw;
  bool stable;
  bool longSent;
  bool repeatArmed;
  bool allowRepeat;
  uint32_t lastChange;
  uint32_t pressStart;
  uint32_t lastRepeat;
};

static Button buttons[3];

static void markPrefsDirty() {
  prefsDirty = true;
  prefsDirtyAt = millis();
}

static void savePrefsIfNeeded() {
  if (!prefsDirty || (millis() - prefsDirtyAt) < 600) {
    return;
  }
  const uint8_t mode = Leds::mode();
  const uint8_t bri = Leds::brightness();
  const bool on = Leds::on();
  if (mode == lastSavedMode && bri == lastSavedBri && on == lastSavedOn) {
    prefsDirty = false;
    return;
  }
  prefs.putUChar("mode", mode);
  prefs.putUChar("bri", bri);
  prefs.putBool("on", on);
  lastSavedMode = mode;
  lastSavedBri = bri;
  lastSavedOn = on;
  prefsDirty = false;
}

static void loadPrefs() {
  const uint8_t mode = prefs.getUChar("mode", MODE_SOLID);
  const uint8_t bri = prefs.getUChar("bri", BRIGHTNESS_DEFAULT);
  const bool on = prefs.getBool("on", true);
  Leds::setMode(mode);
  Leds::setBrightness(bri);
  Leds::setOn(on);
  lastSavedMode = Leds::mode();
  lastSavedBri = Leds::brightness();
  lastSavedOn = Leds::on();
}

static void setupButtons() {
  const int pins[3] = {PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_SELECT};
  for (int i = 0; i < 3; i++) {
    buttons[i] = {};
    buttons[i].pin = pins[i];
    pinMode(pins[i], INPUT_PULLUP);
    const bool pressed = digitalRead(pins[i]) == LOW;
    buttons[i].lastRaw = pressed;
    buttons[i].stable = pressed;
    buttons[i].lastChange = millis();
    buttons[i].allowRepeat = (i < 2);  // left and right auto-repeat
  }
}

static bool pressedLevel(int pin) {
  const int v = digitalRead(pin);
  return BUTTON_INVERT ? (v == LOW) : (v == HIGH);
}

static BtnEvent pollButton(Button &b) {
  const uint32_t now = millis();
  const bool raw = pressedLevel(b.pin);
  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
  }
  if ((now - b.lastChange) < DEBOUNCE_MS) {
    return BTN_NONE;
  }

  if (raw != b.stable) {
    b.stable = raw;
    if (b.stable) {
      b.pressStart = now;
      b.longSent = false;
      b.repeatArmed = false;
      b.lastRepeat = now;
    } else if (!b.longSent) {
      return BTN_CLICK;
    }
    return BTN_NONE;
  }

  if (!b.stable) {
    return BTN_NONE;
  }

  if (!b.longSent && (now - b.pressStart) >= LONG_PRESS_MS) {
    b.longSent = true;
    if (b.allowRepeat) {
      b.repeatArmed = true;
      b.lastRepeat = now;
    }
    return BTN_LONG;
  }

  if (!b.allowRepeat) {
    return BTN_NONE;
  }

  if (b.repeatArmed && (now - b.lastRepeat) >= REPEAT_MS) {
    b.lastRepeat = now;
    return BTN_REPEAT;
  }

  if (!b.repeatArmed && (now - b.pressStart) >= REPEAT_FIRST_MS) {
    b.repeatArmed = true;
    b.longSent = true;
    b.lastRepeat = now;
    return BTN_REPEAT;
  }

  return BTN_NONE;
}

static void handleButtons() {
  const uint8_t modeBefore = Leds::mode();
  const uint8_t briBefore = Leds::brightness();
  const bool onBefore = Leds::on();

  for (int i = 0; i < 3; i++) {
    const BtnEvent ev = pollButton(buttons[i]);
    if (ev == BTN_NONE) {
      continue;
    }
    if (i == 0) {
      if (ev == BTN_CLICK) {
        Ui::onLeft();
      } else if (ev == BTN_REPEAT) {
        Ui::onLeftRepeat();
      }
    } else if (i == 1) {
      if (ev == BTN_CLICK) {
        Ui::onRight();
      } else if (ev == BTN_REPEAT) {
        Ui::onRightRepeat();
      }
    } else {
      if (ev == BTN_CLICK) {
        Ui::onSelect();
      } else if (ev == BTN_LONG) {
        Ui::onSelectLong();
      }
    }
  }

  if (Leds::mode() != modeBefore || Leds::brightness() != briBefore ||
      Leds::on() != onBefore) {
    markPrefsDirty();
  }
}

static void handleSerial() {
  if (!Serial.available()) {
    return;
  }
  const char c = (char)Serial.read();
  if (c == 'n' || c == 'N') {
    Leds::setMode((uint8_t)((Leds::mode() + 1) % MODE_COUNT));
    markPrefsDirty();
    Serial.printf("mode %s\n", Leds::modeName());
  } else if (c == 'b' || c == 'B') {
    Leds::nudgeBrightness(BRIGHTNESS_STEP);
    markPrefsDirty();
    Serial.printf("brightness %u\n", Leds::brightness());
  } else if (c == 'o' || c == 'O') {
    Leds::toggle();
    markPrefsDirty();
    Serial.printf("lights %s\n", Leds::on() ? "on" : "off");
  } else if (c == 's' || c == 'S') {
    Serial.printf("on=%d mode=%s bri=%u\n", Leds::on(), Leds::modeName(),
                  Leds::brightness());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nhelmet-esp32 %s  XIAO  USB power bank\n", HELMET_FW_VERSION);
  Serial.printf("PWM D10/GPIO%d  OLED SDA GPIO%d SCL GPIO%d\n", PIN_LED_PWM, PIN_SDA,
                PIN_SCL);
  Serial.printf("buttons L/R/S GPIO %d %d %d  mic GPIO%d\n", PIN_BTN_LEFT,
                PIN_BTN_RIGHT, PIN_BTN_SELECT, PIN_MIC);

  Leds::begin();
  setupButtons();
  prefs.begin(PREFS_NAMESPACE, false);
  loadPrefs();
  Ui::begin();
}

void loop() {
  handleButtons();
  handleSerial();
  Leds::loop();
  Ui::loop();
  savePrefsIfNeeded();
  delay(Leds::mode() == MODE_BASS ? 0 : 2);
}
