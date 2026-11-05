// Host preview for the CYD touchscreen UI.
//
// Compiles the *real* src/ui_screens.cpp and src/ui_theme.cpp against LVGL on
// the desktop, renders each page into an offscreen framebuffer and writes it
// out as a PPM. Lets the layout be reviewed (and regressions spotted) without
// flashing a board. Leds::/Ble:: are stubbed here; everything else is the
// firmware's own code.
//
//   make -C tools/sim && tools/sim/build/preview out/

#include <errno.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>

#include "ble.h"
#include "config.h"
#include "leds.h"
#include "ui_screens.h"
#include "ui_theme.h"

// ---------------------------------------------------------------------------
// Fake clock — LV_TICK_CUSTOM reads millis(), so the preview drives time by
// hand and animations land on exactly the frame we want to capture.
// ---------------------------------------------------------------------------

static unsigned long gNow = 0;

extern "C" unsigned long millis(void) { return gNow; }

// ---------------------------------------------------------------------------
// Stubbed firmware state
// ---------------------------------------------------------------------------

namespace {
uint8_t sMode = MODE_PARTY;
uint8_t sBri = 200;
bool sOn = true;
uint8_t sR = 0x00, sG = 0xA8, sB = 0xFF;
uint8_t sLevel = 190;
bool sBleConnected = false;
uint8_t sSpeed = SPEED_DEFAULT;
uint8_t sOrder = LED_ORDER_DEFAULT;
uint16_t sCount = LED_COUNT;
uint16_t sMaxMa = LED_MAX_MA;
uint16_t sSkip = LED_SKIP;
uint8_t sBass[BASS_PARAM_COUNT] = {128, 40, 128, 96, BassDsp::BAND_KICK, BASS_STYLE_METER,
                                   BASS_FLAGS_DEFAULT, 1};
BassDsp::Meter sMeter = {23, 40, 0, 0};

const char *kNames[MODE_COUNT] = {
    "Solid",   "Breathe", "Heartbeat", "Slow blink", "Fast blink", "Strobe",
    "SOS",     "Party",   "Bass",      "Comet",      "Scanner",    "Twinkle",
    "Fire",    "Rainbow", "Wipe",      "Chase"};

const char *kOrderNames[ORDER_COUNT] = {"GRB", "RGB", "BRG", "RBG", "GBR", "BGR"};
}  // namespace

namespace Leds {
void setMode(uint8_t m) { sMode = m; }
void setBrightness(uint8_t b) { sBri = b; }
void setOn(bool on) { sOn = on; }
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  sR = r;
  sG = g;
  sB = b;
}
uint8_t mode() { return sMode; }
uint8_t brightness() { return sBri; }
bool on() { return sOn; }
const char *modeName() { return kNames[sMode]; }
const char *modeNameAt(uint8_t i) { return i < MODE_COUNT ? kNames[i] : ""; }
void setSpeed(uint8_t v) { sSpeed = v; }
void setOrder(uint8_t v) { sOrder = v; }
void setCount(uint16_t v) { sCount = v; }
void setMaxMilliamps(uint16_t v) { sMaxMa = v; }
void setSkip(uint16_t v) {
  sSkip = v > (uint16_t)(sCount - 1u) ? (uint16_t)(sCount - 1u) : v;
}
uint8_t speed() { return sSpeed; }
uint8_t order() { return sOrder; }
uint16_t count() { return sCount; }
uint16_t maxMilliamps() { return sMaxMa; }
uint16_t skip() { return sSkip; }
uint16_t lit() { return (uint16_t)(sCount - sSkip); }
void setBass(uint8_t p, uint8_t v) {
  if (p < BASS_PARAM_COUNT) sBass[p] = v;
}
uint8_t bass(uint8_t p) { return p < BASS_PARAM_COUNT ? sBass[p] : 0; }
BassDsp::Meter bassMeter() { return sMeter; }
bool bassLive() { return sMode == MODE_BASS; }
uint16_t bassCostUs() { return 0; }
void bassCapture(uint16_t *, uint16_t) {}
bool bassCaptureDone() { return true; }
const char *orderName() { return kOrderNames[sOrder]; }
const char *orderNameAt(uint8_t i) { return i < ORDER_COUNT ? kOrderNames[i] : ""; }
uint8_t brightnessPercent() { return (uint8_t)((sBri * 100u) / 255u); }
uint8_t speedPercent() { return (uint8_t)(((uint16_t)sSpeed * 100u) / SPEED_NORMAL); }
uint8_t outputLevel() { return sLevel; }
void color(uint8_t *r, uint8_t *g, uint8_t *b) {
  *r = sR;
  *g = sG;
  *b = sB;
}
}  // namespace Leds

namespace Ble {
bool connected() { return sBleConnected; }
}  // namespace Ble

// ---------------------------------------------------------------------------
// Offscreen display
// ---------------------------------------------------------------------------

namespace {

constexpr int W = UiTheme::SCREEN_W;
constexpr int H = UiTheme::SCREEN_H;

uint8_t gFrame[W * H * 3];
lv_color_t gDrawPixels[W * H];
lv_disp_draw_buf_t gDrawBuf;
lv_disp_drv_t gDispDrv;

inline uint8_t expand(uint32_t v, uint32_t bits) {
  const uint32_t max = (1u << bits) - 1u;
  return (uint8_t)((v * 255u + max / 2u) / max);
}

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
  for (int y = area->y1; y <= area->y2; y++) {
    for (int x = area->x1; x <= area->x2; x++) {
      const uint16_t c = pixels[(y - area->y1) * (area->x2 - area->x1 + 1) +
                                (x - area->x1)]
                             .full;
      uint8_t *px = &gFrame[(y * W + x) * 3];
      px[0] = expand((c >> 11) & 0x1F, 5);
      px[1] = expand((c >> 5) & 0x3F, 6);
      px[2] = expand(c & 0x1F, 5);
    }
  }
  lv_disp_flush_ready(drv);
}

void advance(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 10) {
    gNow += 10;
    lv_timer_handler();
  }
}

void capture(const char *dir, const char *name) {
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(NULL);

  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "cannot write %s\n", path);
    exit(1);
  }
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  fwrite(gFrame, 1, sizeof(gFrame), f);
  fclose(f);
  printf("wrote %s\n", path);
}

// Creates outDir and any parent it needs. The documented invocation points at
// ../../.tmp/preview, which is gitignored and so never exists on a fresh
// checkout; without this the run dies on the first frame with "cannot write".
// Scrolls the tab page currently on screen. Inactive pages still exist off to
// the side of the tabview, so "the first scrollable object" is not good
// enough: the one wanted is the scrollable page whose left edge is at 0.
lv_obj_t *visiblePage(lv_obj_t *o) {
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(o) > 0 &&
      a.x1 == 0) {
    return o;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) {
    if (lv_obj_t *r = visiblePage(lv_obj_get_child(o, i))) return r;
  }
  return nullptr;
}

void scrollVisiblePage(lv_coord_t y) {
  if (lv_obj_t *p = visiblePage(lv_scr_act())) {
    lv_obj_scroll_to_y(p, y, LV_ANIM_OFF);
  }
}

bool makeDirs(const char *path) {
  std::string acc;
  for (const char *p = path;; p++) {
    if (*p == '/' || *p == '\0') {
      if (!acc.empty() && acc != "." && acc != "..") {
        if (mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) {
          return false;
        }
      }
    }
    if (*p == '\0') break;
    acc.push_back(*p);
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  const char *outDir = argc > 1 ? argv[1] : ".";

  if (!makeDirs(outDir)) {
    fprintf(stderr, "cannot create output directory %s: %s\n", outDir,
            strerror(errno));
    return 1;
  }

  lv_init();
  lv_disp_draw_buf_init(&gDrawBuf, gDrawPixels, NULL, W * H);
  lv_disp_drv_init(&gDispDrv);
  gDispDrv.hor_res = W;
  gDispDrv.ver_res = H;
  gDispDrv.flush_cb = flushCb;
  gDispDrv.draw_buf = &gDrawBuf;
  lv_disp_t *disp = lv_disp_drv_register(&gDispDrv);

  lv_disp_set_theme(disp, lv_theme_default_init(disp, UiTheme::col(UiTheme::ION),
                                                UiTheme::col(UiTheme::PHOSPHOR), true,
                                                &at_sans_14));

  UiTheme::init();
  UiScreens::build();
  UiScreens::sync(0);
  UiScreens::showSplash(LOGO_SPLASH_MS);

  // The boot timeline: the mark pops, the wordmark follows, the status line
  // types out, then the card holds and fades to the controls.
  advance(200);
  capture(outDir, "0a-splash-pop");
  advance(800);
  capture(outDir, "0b-splash-typing");
  advance(1000);
  capture(outDir, "1-splash");

  advance(LOGO_SPLASH_MS);  // let the splash hold, fade and delete itself

  UiScreens::showTab(0);
  UiScreens::sync(97);
  advance(200);
  capture(outDir, "2-modes");

  UiScreens::showTab(1);
  advance(200);
  capture(outDir, "3-colour");

  // Bass, idle: any mode but Bass, so the sampler is stopped and the meter
  // says so.
  UiScreens::showTab(2);
  advance(200);
  capture(outDir, "4a-bass-idle");

  // Bass, live, in the middle of a kick: level over the trigger, which sits
  // over the gate, and the kick pip lit.
  sMode = MODE_BASS;
  sMeter = {150, 96, 0, 7};
  UiScreens::sync(97);
  advance(120);  // the meter timer takes its first kick-counter reading
  sMeter = {176, 96, 214, 8};
  advance(60);   // ...and sees it move
  capture(outDir, "4b-bass-kick");

  scrollVisiblePage(140);
  advance(200);
  capture(outDir, "4c-bass-tuning");

  scrollVisiblePage(400);
  advance(200);
  capture(outDir, "4d-bass-options");
  scrollVisiblePage(0);
  sMode = MODE_PARTY;
  UiScreens::sync(97);

  UiScreens::showTab(3);
  advance(200);
  capture(outDir, "5-strip");

  UiScreens::showTab(4);
  sBleConnected = true;
  UiScreens::sync(4337);
  advance(200);
  capture(outDir, "6-system");

  // Off, dimmed, warm white — the other end of the state space.
  UiScreens::showTab(0);
  sMode = MODE_BREATHE;
  sOn = false;
  sBri = 40;
  sR = 0xFF;
  sG = 0xC9;
  sB = 0x8A;
  UiScreens::sync(4337);
  advance(300);
  capture(outDir, "7-modes-off");

  return 0;
}
