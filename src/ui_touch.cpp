#include "ui_touch.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <math.h>
#include <string.h>

#include "config.h"
#include "ui_screens.h"
#include "ui_theme.h"

// Hardware half of the CYD (ESP32-2432S028R) touchscreen UI: panel bring-up,
// backlight, touch calibration, and the two LVGL driver callbacks. The screens
// themselves live in ui_screens.cpp, which stays free of Arduino so the same
// layout can be rendered on the host by tools/sim.
//
// LVGL replaces the earlier hand-rolled TFT_eSPI drawing: it brings
// anti-aliased type, partial (flicker-free) repaints, gradients, shadows and
// animated tab transitions. TFT_eSPI remains underneath as the panel driver,
// and is still used directly for the blocking calibration screens, which run
// before LVGL owns the display.

namespace UiTouch {
namespace {

using namespace UiTheme;

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
bool calibrationRequested = false;

constexpr int BACKLIGHT_CHANNEL = 7;
constexpr uint32_t DRAW_LINES = 40;

lv_disp_draw_buf_t drawBuf;
lv_color_t drawPixels[SCREEN_W * DRAW_LINES];
lv_disp_drv_t dispDrv;
lv_indev_drv_t indevDrv;

// ---------------------------------------------------------------------------
// Touch calibration (blocking, pre-LVGL or on demand)
// ---------------------------------------------------------------------------

// Solves M * [a;b;c] = rhs for a 3x3 system via Cramer's rule.
bool solve3x3(const float m[3][3], const float rhs[3], float out[3]) {
  const float d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                  m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                  m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  if (fabsf(d) < 1e-6f) {
    return false;
  }
  for (int c = 0; c < 3; c++) {
    float mm[3][3];
    memcpy(mm, m, sizeof(mm));
    for (int row = 0; row < 3; row++) {
      mm[row][c] = rhs[row];
    }
    const float dd = mm[0][0] * (mm[1][1] * mm[2][2] - mm[1][2] * mm[2][1]) -
                     mm[0][1] * (mm[1][0] * mm[2][2] - mm[1][2] * mm[2][0]) +
                     mm[0][2] * (mm[1][0] * mm[2][1] - mm[1][1] * mm[2][0]);
    out[c] = dd / d;
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

// Painted with TFT_eSPI directly: this runs either before LVGL exists or as a
// blocking modal, and it must not re-enter lv_timer_handler().
void drawCalibrationTarget(int step, int tx, int ty) {
  const uint16_t bg = tft.color565(0x0B, 0x0E, 0x14);
  tft.fillScreen(bg);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(tft.color565(0xE7, 0xEC, 0xF5), bg);
  tft.drawString("Touch the target", SCREEN_W / 2, SCREEN_H / 2 - 10, 4);

  char sub[16];
  snprintf(sub, sizeof(sub), "%d of 3", step + 1);
  tft.setTextColor(tft.color565(0x6D, 0x7A, 0x90), bg);
  tft.drawString(sub, SCREEN_W / 2, SCREEN_H / 2 + 16, 2);

  const uint16_t accent = tft.color565(0x2F, 0x86, 0xFF);
  tft.drawCircle(tx, ty, 12, tft.color565(0x2B, 0x34, 0x46));
  tft.drawCircle(tx, ty, 7, accent);
  tft.fillCircle(tx, ty, 2, accent);
}

bool performCalibration() {
  const int targets[3][2] = {{28, 28}, {SCREEN_W - 28, 28}, {28, SCREEN_H - 28}};
  float rawX[3], rawY[3];

  for (int i = 0; i < 3; i++) {
    drawCalibrationTarget(i, targets[i][0], targets[i][1]);
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
  if (x > SCREEN_W - 1) x = SCREEN_W - 1;
  if (y < 0) y = 0;
  if (y > SCREEN_H - 1) y = SCREEN_H - 1;
  *outX = x;
  *outY = y;
  return true;
}

// ---------------------------------------------------------------------------
// LVGL drivers
// ---------------------------------------------------------------------------

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors(&pixels->full, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

void touchReadCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  // Resistive panels jitter by a few pixels. A light IIR filter takes the
  // wobble off drags (the slider and the hue ring) without adding felt lag.
  static int smoothX = 0, smoothY = 0;
  static bool wasDown = false;

  int x = 0, y = 0;
  if (!getMappedTouch(&x, &y)) {
    wasDown = false;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = smoothX;
    data->point.y = smoothY;
    return;
  }

  if (!wasDown) {
    smoothX = x;
    smoothY = y;
    wasDown = true;
  } else {
    smoothX += (x - smoothX) / 2;
    smoothY += (y - smoothY) / 2;
  }
  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = smoothX;
  data->point.y = smoothY;
}

void runPendingCalibration() {
  calibrationRequested = false;
  performCalibration();
  // LVGL has no idea TFT_eSPI just painted over it; force a full repaint.
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
}

}  // namespace

void begin() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Take the backlight off TFT_eSPI's plain digitalWrite so the first frame can
  // fade up instead of flashing a half-drawn screen at the user.
  ledcSetup(BACKLIGHT_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, 0);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSpi);

  calPrefs.begin("touchcal", false);
  const size_t got = calPrefs.getBytes("cal", calM, sizeof(calM));
  if (got == sizeof(calM)) {
    calibrated = true;
  } else {
    ledcWrite(BACKLIGHT_CHANNEL, 255);
    performCalibration();
    ledcWrite(BACKLIGHT_CHANNEL, 0);
  }

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, drawPixels, nullptr, SCREEN_W * DRAW_LINES);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = SCREEN_W;
  dispDrv.ver_res = SCREEN_H;
  dispDrv.flush_cb = flushCb;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_t *disp = lv_disp_drv_register(&dispDrv);

  lv_indev_drv_init(&indevDrv);
  indevDrv.type = LV_INDEV_TYPE_POINTER;
  indevDrv.read_cb = touchReadCb;
  lv_indev_drv_register(&indevDrv);

  // Seed LVGL's own dark theme with our accent so stock widgets (the switch,
  // the tab indicator) land in the same palette as the custom styles.
  lv_disp_set_theme(disp, lv_theme_default_init(disp, col(ACCENT), col(ACCENT_LT), true,
                                                &lv_font_montserrat_14));

  UiTheme::init();
  UiScreens::build();
  UiScreens::sync(0);
  UiScreens::showSplash(SPLASH_MS);

  lv_timer_handler();  // paint the splash before the backlight comes up
  for (int duty = 0; duty <= 255; duty += 15) {
    ledcWrite(BACKLIGHT_CHANNEL, duty);
    delay(8);
  }
  ledcWrite(BACKLIGHT_CHANNEL, 255);

  Serial.println("CYD display+touch ok (LVGL)");
}

void calibrate() { calibrationRequested = true; }

void printTouchDebug() {
  Serial.println("raw touch for 8s, press the screen now:");
  const uint32_t end = millis() + 8000;
  while (millis() < end) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int mx = -1, my = -1;
      getMappedTouch(&mx, &my);
      Serial.printf("touched=1 raw x=%d y=%d z=%d  mapped x=%d y=%d\n", p.x, p.y, p.z,
                    mx, my);
    } else {
      Serial.println("touched=0");
    }
    delay(150);
  }
}

void loop() {
  if (calibrationRequested || UiScreens::takeCalibrationRequest()) {
    runPendingCalibration();
  }

  static uint32_t lastSync = 0;
  const uint32_t now = millis();
  if (now - lastSync >= 80) {
    lastSync = now;
    UiScreens::sync(now / 1000u);
  }
  lv_timer_handler();
}

}  // namespace UiTouch
