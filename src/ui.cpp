#include "ui.h"

#include <cstdio>

#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"
#include "display.h"
#include "leds.h"

namespace Ui {
namespace {

enum Screen : uint8_t { SCREEN_SPLASH, SCREEN_STATUS, SCREEN_MENU, SCREEN_BRIGHTNESS };

enum MenuId : uint8_t {
  // 0 .. MODE_COUNT-1 are LED modes
  MENU_BRIGHTNESS = MODE_COUNT,
  MENU_POWER,
  MENU_COUNT
};

#if OLED_HEIGHT >= 64
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

Screen screen = SCREEN_SPLASH;
uint8_t menuIndex = 0;
uint32_t screenAt = 0;
uint32_t lastInput = 0;
uint32_t lastStatusDraw = 0;
bool present = false;
bool dirty = true;
// Pocket mode (see display.h). What the panel is currently doing, so the
// transition runs once rather than on every pass.
bool screenLit = true;

void markDirty() { dirty = true; }

void touch() {
  lastInput = millis();
  markDirty();
}

void drawCentered(const char *text, int y) {
  const int w = u8g2.getStrWidth(text);
  u8g2.drawStr((OLED_WIDTH - w) / 2, y, text);
}

void drawChevrons() {
  u8g2.drawTriangle(1, 20, 8, 14, 8, 26);
  u8g2.drawTriangle(126, 20, 119, 14, 119, 26);
}

void drawBrightnessBar(int y, int h) {
  const int x = 10;
  const int w = OLED_WIDTH - 20;
  u8g2.drawFrame(x, y, w, h);
  const int fill = ((int)Leds::brightness() * (w - 4)) / 255;
  if (fill > 0) {
    u8g2.drawBox(x + 2, y + 2, fill, h - 4);
  }
}

const char *menuLabel(uint8_t index) {
  if (index < MODE_COUNT) {
    return Leds::modeNameAt(index);
  }
  if (index == MENU_BRIGHTNESS) {
    return "Brightness";
  }
  return "Lights";
}

const char *menuHint(uint8_t index) {
  if (index == MODE_BASS) {
    return "MIC";
  }
  if (index < MODE_COUNT) {
    return "MODE";
  }
  if (index == MENU_BRIGHTNESS) {
    return "ADJUST";
  }
  return "POWER";
}

void drawSplash() {
  u8g2.setFont(u8g2_font_7x14B_tf);
  drawCentered("HELMET", 14);
  u8g2.setFont(u8g2_font_5x8_tf);
  drawCentered("USB  v" HELMET_FW_VERSION, 28);
}

void drawStatus() {
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 13, Leds::modeName());

  u8g2.setFont(u8g2_font_5x8_tf);
  const char *pwr = Leds::on() ? "ON" : "OFF";
  const int pw = u8g2.getStrWidth(pwr);
  u8g2.drawStr(OLED_WIDTH - pw, 10, pwr);

  char pct[8];
  snprintf(pct, sizeof(pct), "%u%%", Leds::brightnessPercent());
  const int tw = u8g2.getStrWidth(pct);
  u8g2.drawStr(OLED_WIDTH - tw, 26, pct);
  const int barW = OLED_WIDTH - tw - 16;
  u8g2.drawFrame(0, 18, barW, 10);
  const int fill = ((int)Leds::outputLevel() * (barW - 4)) / 255;
  if (fill > 0) {
    u8g2.drawBox(2, 20, fill, 6);
  }
}

void drawMenu() {
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 8, menuHint(menuIndex));

  if (menuIndex < MODE_COUNT && menuIndex == Leds::mode() && Leds::on()) {
    u8g2.drawDisc(OLED_WIDTH - 6, 5, 3);
  } else if (menuIndex == MENU_POWER) {
    const char *pwr = Leds::on() ? "ON" : "OFF";
    const int pw = u8g2.getStrWidth(pwr);
    u8g2.drawStr(OLED_WIDTH - pw, 8, pwr);
  } else if (menuIndex == MENU_BRIGHTNESS) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", Leds::brightnessPercent());
    const int pw = u8g2.getStrWidth(pct);
    u8g2.drawStr(OLED_WIDTH - pw, 8, pct);
  }

  drawChevrons();
  u8g2.setFont(u8g2_font_7x14B_tf);
  const char *label = menuLabel(menuIndex);
  if (menuIndex == MENU_POWER) {
    label = Leds::on() ? "Turn off" : "Turn on";
  }
  drawCentered(label, 28);
}

void drawBrightness() {
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 8, "BRIGHTNESS");
  char pct[8];
  snprintf(pct, sizeof(pct), "%u%%", Leds::brightnessPercent());
  const int pw = u8g2.getStrWidth(pct);
  u8g2.drawStr(OLED_WIDTH - pw, 8, pct);
  drawChevrons();
  drawBrightnessBar(14, 14);
}

void render() {
  if (!present) {
    return;
  }
  u8g2.clearBuffer();
  switch (screen) {
    case SCREEN_SPLASH:
      drawSplash();
      break;
    case SCREEN_STATUS:
      drawStatus();
      break;
    case SCREEN_MENU:
      drawMenu();
      break;
    case SCREEN_BRIGHTNESS:
      drawBrightness();
      break;
  }
  u8g2.sendBuffer();
  dirty = false;
}

void gotoScreen(Screen next) {
  screen = next;
  screenAt = millis();
  markDirty();
}

void stepMenu(int delta) {
  int next = (int)menuIndex + delta;
  while (next < 0) {
    next += MENU_COUNT;
  }
  menuIndex = (uint8_t)(next % MENU_COUNT);
  gotoScreen(SCREEN_MENU);
}

void applyMenuSelect() {
  if (menuIndex < MODE_COUNT) {
    Leds::setMode(menuIndex);
    markDirty();
    return;
  }
  if (menuIndex == MENU_BRIGHTNESS) {
    gotoScreen(SCREEN_BRIGHTNESS);
    return;
  }
  Leds::toggle();
  markDirty();
}

}  // namespace

void begin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  Wire.beginTransmission(OLED_ADDRESS);
  present = (Wire.endTransmission() == 0);
  if (!present) {
    Wire.beginTransmission(0x3D);
    present = (Wire.endTransmission() == 0);
    if (present) {
      u8g2.setI2CAddress(0x3D << 1);
    }
  } else {
    u8g2.setI2CAddress(OLED_ADDRESS << 1);
  }

  if (!present) {
    Serial.println("OLED not found on I2C (checked 0x3C and 0x3D)");
    return;
  }

  u8g2.begin();
  if (OLED_ROTATION == 2) {
    u8g2.setDisplayRotation(U8G2_R2);
  } else if (OLED_ROTATION == 1) {
    u8g2.setDisplayRotation(U8G2_R1);
  } else if (OLED_ROTATION == 3) {
    u8g2.setDisplayRotation(U8G2_R3);
  }
  u8g2.setContrast(255);
  u8g2.setFontPosBaseline();
  lastInput = millis();

  if (!Display::on()) {
    // Pocket mode survived the reboot: come up dark rather than showing the
    // splash to the inside of a pocket. The app switches it back on.
    screenLit = false;
    u8g2.setPowerSave(1);
    Serial.println("OLED off (pocket mode) -- switch it on from the app");
    return;
  }

  gotoScreen(SCREEN_SPLASH);
  render();
  Serial.println("OLED ok");
}

void loop() {
  if (!present) {
    return;
  }

  // Pocket mode: the panel is powered down and the buttons are dropped in
  // main.cpp, so there is nothing to draw and nothing that could change what
  // would be drawn from here.
  if (!Display::on()) {
    if (screenLit) {
      screenLit = false;
      u8g2.setPowerSave(1);
      Serial.println("OLED off (pocket mode)");
    }
    return;
  }
  if (!screenLit) {
    screenLit = true;
    u8g2.setPowerSave(0);
    // Back to the status screen rather than wherever the menu was left: the
    // helmet has been in a pocket, and a half-open menu is not where anyone
    // wants to pick it up.
    gotoScreen(SCREEN_STATUS);
    Serial.println("OLED on");
  }

  const uint32_t now = millis();
  if (screen == SCREEN_SPLASH && (now - screenAt) >= SPLASH_MS) {
    gotoScreen(SCREEN_STATUS);
  }
  if (screen == SCREEN_MENU && (now - lastInput) >= MENU_IDLE_MS) {
    gotoScreen(SCREEN_STATUS);
  }
  if (screen == SCREEN_STATUS && (now - lastStatusDraw) >= 70) {
    lastStatusDraw = now;
    markDirty();
  }
  if (dirty) {
    render();
  }
}

void onLeft() {
  touch();
  if (screen == SCREEN_SPLASH) {
    gotoScreen(SCREEN_MENU);
    return;
  }
  if (screen == SCREEN_STATUS) {
    menuIndex = Leds::mode();
    gotoScreen(SCREEN_MENU);
    return;
  }
  if (screen == SCREEN_BRIGHTNESS) {
    Leds::nudgeBrightness(-BRIGHTNESS_STEP);
    markDirty();
    return;
  }
  stepMenu(-1);
}

void onRight() {
  touch();
  if (screen == SCREEN_SPLASH) {
    gotoScreen(SCREEN_MENU);
    return;
  }
  if (screen == SCREEN_STATUS) {
    menuIndex = Leds::mode();
    gotoScreen(SCREEN_MENU);
    return;
  }
  if (screen == SCREEN_BRIGHTNESS) {
    Leds::nudgeBrightness(BRIGHTNESS_STEP);
    markDirty();
    return;
  }
  stepMenu(1);
}

void onSelect() {
  touch();
  if (screen == SCREEN_SPLASH || screen == SCREEN_STATUS) {
    menuIndex = Leds::mode();
    gotoScreen(SCREEN_MENU);
    return;
  }
  if (screen == SCREEN_BRIGHTNESS) {
    gotoScreen(SCREEN_MENU);
    return;
  }
  applyMenuSelect();
}

void onLeftRepeat() {
  if (screen == SCREEN_BRIGHTNESS || screen == SCREEN_MENU) {
    onLeft();
  }
}

void onRightRepeat() {
  if (screen == SCREEN_BRIGHTNESS || screen == SCREEN_MENU) {
    onRight();
  }
}

void onSelectLong() {
  touch();
  if (screen == SCREEN_STATUS) {
    Leds::toggle();
    markDirty();
    return;
  }
  gotoScreen(SCREEN_STATUS);
}

}  // namespace Ui
