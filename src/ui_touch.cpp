#include "ui_touch.h"

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "leds.h"

namespace UiTouch {
namespace {

TFT_eSPI tft = TFT_eSPI();
Preferences calPrefs;

// Touch has its own SPI bus, separate from the display (see config.h) — do
// not route it through the TFT's SPI instance.
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// General affine map from raw touch ADC counts to screen pixels, solved from
// 3 calibration points. Handles whatever combination of axis swap/mirror/
// rotation this board's touch panel happens to have relative to the display,
// without needing to know it in advance.
//   screenX = calM[0]*rawX + calM[1]*rawY + calM[2]
//   screenY = calM[3]*rawX + calM[4]*rawY + calM[5]
float calM[6] = {0, 0, 0, 0, 0, 0};
bool calibrated = false;

enum Screen : uint8_t { SCREEN_SPLASH, SCREEN_MAIN, SCREEN_COLOR };
Screen screen = SCREEN_SPLASH;
uint32_t screenAt = 0;
bool dirty = true;
bool colorScreenDrawn = false;
bool touchWasDown = false;

constexpr int W = 320;
constexpr int H = 240;

constexpr int HEADER_H = 34;
constexpr int GRID_TOP = HEADER_H + 6;
constexpr int GRID_COLS = 3;
constexpr int GRID_ROWS = 3;
constexpr int GRID_GAP = 5;
constexpr int GRID_CELL_W = (W - GRID_GAP * (GRID_COLS + 1)) / GRID_COLS;
constexpr int GRID_CELL_H = 36;
constexpr int GRID_BOTTOM = GRID_TOP + GRID_ROWS * (GRID_CELL_H + GRID_GAP);

constexpr int SLIDER_LABEL_TOP = GRID_BOTTOM + 6;
constexpr int SLIDER_TRACK_Y = SLIDER_LABEL_TOP + 18;
constexpr int SLIDER_TRACK_H = 10;
constexpr int SLIDER_TRACK_X = 10;
constexpr int SLIDER_TRACK_W = W - 20;

constexpr int FOOTER_TOP = SLIDER_TRACK_Y + SLIDER_TRACK_H + 14;
constexpr int FOOTER_H = H - FOOTER_TOP - 8;
constexpr int FOOTER_BTN_W = (W - 24) / 2;

constexpr int WHEEL_CX = 160;
constexpr int WHEEL_CY = 140;
constexpr int WHEEL_R = 92;
constexpr int BACK_BTN_X = 8, BACK_BTN_Y = 8, BACK_BTN_W = 70, BACK_BTN_H = 34;
constexpr int SWATCH_X = W - 90, SWATCH_Y = 8, SWATCH_W = 82, SWATCH_H = 34;

// Cached state, used to notice changes made by BLE/serial while this screen
// isn't the one doing the drawing.
uint8_t cacheMode = 0xFF, cacheBri = 0xFF, cacheR = 0xFF, cacheG = 0xFF, cacheB = 0xFF;
bool cacheOn = true;

void markDirty() { dirty = true; }

bool within(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

int modeButtonIndex(int x, int y) {
  if (y < GRID_TOP || y >= GRID_BOTTOM) {
    return -1;
  }
  const int row = (y - GRID_TOP) / (GRID_CELL_H + GRID_GAP);
  const int col = (x - GRID_GAP) / (GRID_CELL_W + GRID_GAP);
  if (row < 0 || row >= GRID_ROWS || col < 0 || col >= GRID_COLS) {
    return -1;
  }
  const int cellX = GRID_GAP + col * (GRID_CELL_W + GRID_GAP);
  const int cellY = GRID_TOP + row * (GRID_CELL_H + GRID_GAP);
  if (!within(x, y, cellX, cellY, GRID_CELL_W, GRID_CELL_H)) {
    return -1;
  }
  const int idx = row * GRID_COLS + col;
  return idx < MODE_COUNT ? idx : -1;
}

void colorAtWheel(int dx, int dy, uint8_t *r, uint8_t *g, uint8_t *b) {
  const float radius = sqrtf((float)(dx * dx + dy * dy));
  const float angle = atan2f((float)dy, (float)dx);
  const uint8_t hue = (uint8_t)(((angle + PI) / (2.0f * PI)) * 255.0f);
  float satF = (radius / (float)WHEEL_R) * 255.0f;
  if (satF > 255.0f) satF = 255.0f;
  CRGB rgb;
  hsv2rgb_rainbow(CHSV(hue, (uint8_t)satF, 255), rgb);
  *r = rgb.r;
  *g = rgb.g;
  *b = rgb.b;
}

void drawSwatch() {
  uint8_t r, g, b;
  Leds::color(&r, &g, &b);
  tft.fillRoundRect(SWATCH_X, SWATCH_Y, SWATCH_W, SWATCH_H, 6, tft.color565(r, g, b));
  tft.drawRoundRect(SWATCH_X, SWATCH_Y, SWATCH_W, SWATCH_H, 6, TFT_WHITE);
}

void drawHeader() {
  tft.fillRect(0, 0, W, HEADER_H, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString(Leds::modeName(), 10, HEADER_H / 2, 4);

  const bool on = Leds::on();
  const uint16_t bg = on ? TFT_DARKGREEN : TFT_MAROON;
  tft.fillRoundRect(W - 92, 5, 84, HEADER_H - 10, 6, bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, bg);
  tft.drawString(on ? "ON" : "OFF", W - 50, HEADER_H / 2, 2);
}

void drawModeGrid() {
  for (int i = 0; i < GRID_ROWS * GRID_COLS; i++) {
    const int row = i / GRID_COLS;
    const int col = i % GRID_COLS;
    const int x = GRID_GAP + col * (GRID_CELL_W + GRID_GAP);
    const int y = GRID_TOP + row * (GRID_CELL_H + GRID_GAP);
    if (i >= MODE_COUNT) {
      tft.fillRoundRect(x, y, GRID_CELL_W, GRID_CELL_H, 6, TFT_BLACK);
      continue;
    }
    const bool active = (i == Leds::mode());
    const uint16_t bg = active ? TFT_ORANGE : TFT_DARKGREY;
    tft.fillRoundRect(x, y, GRID_CELL_W, GRID_CELL_H, 6, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(active ? TFT_BLACK : TFT_WHITE, bg);
    tft.drawString(Leds::modeNameAt(i), x + GRID_CELL_W / 2, y + GRID_CELL_H / 2, 2);
  }
}

void drawSlider() {
  char buf[20];
  snprintf(buf, sizeof(buf), "Brightness %u%%", Leds::brightnessPercent());
  tft.fillRect(0, SLIDER_LABEL_TOP, W, 16, TFT_BLACK);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 10, SLIDER_LABEL_TOP + 8, 2);

  tft.fillRoundRect(SLIDER_TRACK_X, SLIDER_TRACK_Y, SLIDER_TRACK_W, SLIDER_TRACK_H, 5,
                     TFT_DARKGREY);
  const int fillW = ((int)Leds::brightness() * SLIDER_TRACK_W) / 255;
  if (fillW > 0) {
    tft.fillRoundRect(SLIDER_TRACK_X, SLIDER_TRACK_Y, fillW, SLIDER_TRACK_H, 5,
                       TFT_SKYBLUE);
  }
  tft.fillCircle(SLIDER_TRACK_X + fillW, SLIDER_TRACK_Y + SLIDER_TRACK_H / 2, 11,
                 TFT_WHITE);
}

void drawFooter() {
  tft.fillRoundRect(8, FOOTER_TOP, FOOTER_BTN_W, FOOTER_H, 8, TFT_PURPLE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_PURPLE);
  tft.drawString("Color", 8 + FOOTER_BTN_W / 2, FOOTER_TOP + FOOTER_H / 2, 4);

  uint8_t r, g, b;
  Leds::color(&r, &g, &b);
  const int x2 = 16 + FOOTER_BTN_W;
  tft.fillRoundRect(x2, FOOTER_TOP, FOOTER_BTN_W, FOOTER_H, 8, tft.color565(r, g, b));
  tft.drawRoundRect(x2, FOOTER_TOP, FOOTER_BTN_W, FOOTER_H, 8, TFT_WHITE);
}

void drawMain() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  drawModeGrid();
  drawSlider();
  drawFooter();
}

void drawSplash() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("HELMET", W / 2, H / 2 - 12, 6);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("v" HELMET_FW_VERSION, W / 2, H / 2 + 24, 2);
}

void drawColorWheel() {
  tft.fillScreen(TFT_BLACK);
  tft.startWrite();
  for (int dy = -WHEEL_R; dy <= WHEEL_R; dy++) {
    for (int dx = -WHEEL_R; dx <= WHEEL_R; dx++) {
      if (dx * dx + dy * dy > WHEEL_R * WHEEL_R) {
        continue;
      }
      uint8_t r, g, b;
      colorAtWheel(dx, dy, &r, &g, &b);
      tft.drawPixel(WHEEL_CX + dx, WHEEL_CY + dy, tft.color565(r, g, b));
    }
  }
  tft.endWrite();

  tft.fillRoundRect(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H, 6, TFT_DARKGREY);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("Back", BACK_BTN_X + BACK_BTN_W / 2, BACK_BTN_Y + BACK_BTN_H / 2, 2);

  drawSwatch();
}

void loadCache() {
  cacheMode = Leds::mode();
  cacheBri = Leds::brightness();
  cacheOn = Leds::on();
  Leds::color(&cacheR, &cacheG, &cacheB);
}

void checkExternalChanges() {
  uint8_t r, g, b;
  Leds::color(&r, &g, &b);
  if (Leds::mode() != cacheMode || Leds::brightness() != cacheBri ||
      Leds::on() != cacheOn || r != cacheR || g != cacheG || b != cacheB) {
    loadCache();
    markDirty();
  }
}

void handleMainTouch(int x, int y, bool isNewPress) {
  const int sliderHitTop = SLIDER_TRACK_Y - 12;
  const int sliderHitH = SLIDER_TRACK_H + 24;
  if (within(x, y, SLIDER_TRACK_X - 12, sliderHitTop, SLIDER_TRACK_W + 24, sliderHitH)) {
    int rel = x - SLIDER_TRACK_X;
    if (rel < 0) rel = 0;
    if (rel > SLIDER_TRACK_W) rel = SLIDER_TRACK_W;
    Leds::setBrightness((uint8_t)((rel * 255) / SLIDER_TRACK_W));
    loadCache();
    markDirty();
    return;
  }

  if (!isNewPress) {
    return;
  }

  if (within(x, y, W - 92, 5, 84, HEADER_H - 10)) {
    Leds::toggle();
    loadCache();
    markDirty();
    return;
  }

  const int idx = modeButtonIndex(x, y);
  if (idx >= 0) {
    Leds::setMode((uint8_t)idx);
    loadCache();
    markDirty();
    return;
  }

  if (within(x, y, 8, FOOTER_TOP, FOOTER_BTN_W, FOOTER_H)) {
    screen = SCREEN_COLOR;
    colorScreenDrawn = false;
    return;
  }
}

void handleColorTouch(int x, int y, bool isNewPress) {
  if (isNewPress && within(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    screen = SCREEN_MAIN;
    markDirty();
    return;
  }
  const int dx = x - WHEEL_CX;
  const int dy = y - WHEEL_CY;
  if (dx * dx + dy * dy > WHEEL_R * WHEEL_R) {
    return;
  }
  uint8_t r, g, b;
  colorAtWheel(dx, dy, &r, &g, &b);
  Leds::setColor(r, g, b);
  loadCache();
  drawSwatch();
}

// Solves M * [a;b;c] = rhs for a 3x3 system via Cramer's rule.
bool solve3x3(const float m[3][3], const float rhs[3], float out[3]) {
  const float d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                  m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                  m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  if (fabsf(d) < 1e-6f) {
    return false;
  }
  for (int col = 0; col < 3; col++) {
    float mm[3][3];
    memcpy(mm, m, sizeof(mm));
    for (int row = 0; row < 3; row++) {
      mm[row][col] = rhs[row];
    }
    const float dd = mm[0][0] * (mm[1][1] * mm[2][2] - mm[1][2] * mm[2][1]) -
                      mm[0][1] * (mm[1][0] * mm[2][2] - mm[1][2] * mm[2][0]) +
                      mm[0][2] * (mm[1][0] * mm[2][1] - mm[1][1] * mm[2][0]);
    out[col] = dd / d;
  }
  return true;
}

bool waitForTouchPoint(float *rawX, float *rawY, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (!ts.touched()) {
    if (millis() - start > timeoutMs) {
      return false;
    }
    delay(10);
  }
  float sx = 0, sy = 0;
  int n = 0;
  const uint32_t sampleEnd = millis() + 200;
  while (millis() < sampleEnd) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      sx += p.x;
      sy += p.y;
      n++;
    }
    delay(10);
  }
  if (n == 0) {
    return false;
  }
  *rawX = sx / n;
  *rawY = sy / n;
  while (ts.touched()) {
    delay(10);
  }
  return true;
}

bool performCalibration() {
  const int targets[3][2] = {{30, 30}, {W - 30, 30}, {30, H - 30}};
  float rawX[3], rawY[3];

  for (int i = 0; i < 3; i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Touch the crosshair", W / 2, H / 2 - 40, 2);
    tft.drawLine(targets[i][0] - 10, targets[i][1], targets[i][0] + 10, targets[i][1],
                 TFT_ORANGE);
    tft.drawLine(targets[i][0], targets[i][1] - 10, targets[i][0], targets[i][1] + 10,
                 TFT_ORANGE);
    if (!waitForTouchPoint(&rawX[i], &rawY[i], 30000)) {
      Serial.println("touch calibration timed out");
      return false;
    }
  }

  float m[3][3];
  float rhsX[3], rhsY[3];
  for (int i = 0; i < 3; i++) {
    m[i][0] = rawX[i];
    m[i][1] = rawY[i];
    m[i][2] = 1.0f;
    rhsX[i] = (float)targets[i][0];
    rhsY[i] = (float)targets[i][1];
  }
  float coefX[3], coefY[3];
  if (!solve3x3(m, rhsX, coefX) || !solve3x3(m, rhsY, coefY)) {
    Serial.println("touch calibration math failed (collinear points?)");
    return false;
  }
  calM[0] = coefX[0];
  calM[1] = coefX[1];
  calM[2] = coefX[2];
  calM[3] = coefY[0];
  calM[4] = coefY[1];
  calM[5] = coefY[2];
  calibrated = true;
  calPrefs.putBytes("cal", calM, sizeof(calM));
  Serial.println("touch calibration saved");
  return true;
}

bool getMappedTouch(int *outX, int *outY) {
  if (!calibrated || !ts.touched()) {
    return false;
  }
  TS_Point p = ts.getPoint();
  int x = (int)(calM[0] * p.x + calM[1] * p.y + calM[2]);
  int y = (int)(calM[3] * p.x + calM[4] * p.y + calM[5]);
  if (x < 0) x = 0;
  if (x > W - 1) x = W - 1;
  if (y < 0) y = 0;
  if (y > H - 1) y = H - 1;
  *outX = x;
  *outY = y;
  return true;
}

}  // namespace

void begin() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);

  calPrefs.begin("touchcal", false);
  const size_t got = calPrefs.getBytes("cal", calM, sizeof(calM));
  if (got == sizeof(calM)) {
    calibrated = true;
  } else {
    performCalibration();
  }

  loadCache();
  screen = SCREEN_SPLASH;
  screenAt = millis();
  drawSplash();
  Serial.println("CYD display+touch ok");
}

void calibrate() {
  performCalibration();
  markDirty();
}

void printTouchDebug() {
  Serial.println("raw touch for 8s, press the screen now:");
  const uint32_t end = millis() + 8000;
  while (millis() < end) {
    const bool touched = ts.touched();
    if (touched) {
      TS_Point p = ts.getPoint();
      int mx = -1, my = -1;
      getMappedTouch(&mx, &my);
      Serial.printf("touched=1 raw x=%d y=%d z=%d  mapped x=%d y=%d\n", p.x, p.y, p.z, mx,
                    my);
    } else {
      Serial.println("touched=0");
    }
    delay(150);
  }
}

void loop() {
  const uint32_t now = millis();
  if (screen == SCREEN_SPLASH) {
    if ((now - screenAt) >= SPLASH_MS) {
      screen = SCREEN_MAIN;
      dirty = true;
    } else {
      return;
    }
  }

  int tx = 0, ty = 0;
  const bool pressed = getMappedTouch(&tx, &ty);
  const bool isNewPress = pressed && !touchWasDown;

  if (screen == SCREEN_MAIN) {
    if (pressed) {
      handleMainTouch(tx, ty, isNewPress);
    }
    checkExternalChanges();
    if (dirty) {
      drawMain();
      dirty = false;
    }
  } else if (screen == SCREEN_COLOR) {
    if (!colorScreenDrawn) {
      drawColorWheel();
      colorScreenDrawn = true;
    }
    if (pressed) {
      handleColorTouch(tx, ty, isNewPress);
    }
  }

  touchWasDown = pressed;
}

}  // namespace UiTouch
