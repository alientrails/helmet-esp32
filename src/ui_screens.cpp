#include "ui_screens.h"

#include <stdint.h>
#include <stdio.h>

#include "ble.h"
#include "config.h"
#include "leds.h"
#include "ui_theme.h"

// Layout, 320x240 landscape:
//   0..36    top bar    — wordmark, live mode, BLE state, power switch
//   36..39   pulse      — live PWM output, painted in the strip's own colour
//   39..80   brightness — eyebrow label, percentage, slider
//   80..204  tab pages  — Modes (3x3 grid) / Colour (ring + presets) / System
//   204..240 tab bar

namespace UiScreens {
namespace {

using namespace UiTheme;

lv_obj_t *tabview = nullptr;
lv_obj_t *modeTitle = nullptr;
lv_obj_t *bleIcon = nullptr;
lv_obj_t *powerSwitch = nullptr;
lv_obj_t *pulseBar = nullptr;
lv_obj_t *briSlider = nullptr;
lv_obj_t *briValue = nullptr;
lv_obj_t *modeBtn[MODE_COUNT] = {nullptr};
lv_obj_t *modeBtnLabel[MODE_COUNT] = {nullptr};
lv_obj_t *wheel = nullptr;
lv_obj_t *wheelDot = nullptr;
lv_obj_t *wheelHex = nullptr;
lv_obj_t *sysBleValue = nullptr;
lv_obj_t *sysUptimeValue = nullptr;

bool calibrationRequested = false;

// Mirrors of the LED state, so the refresh only touches LVGL (and so only
// repaints) when something actually moved. BLE and serial can change the same
// state behind our back, which is exactly what this catches.
uint8_t shownMode = 0xFF;
uint8_t shownBri = 0xFF;
bool shownOn = false;
uint8_t shownR = 0, shownG = 0, shownB = 0;
bool shownColorValid = false;
bool shownBle = false;
bool shownBleValid = false;
uint32_t shownUptimeSec = 0xFFFFFFFF;

// A 12-swatch palette spanning the useful gamut, including a warm and a neutral
// white — the two things a hue ring alone cannot give you.
constexpr uint32_t kPresets[] = {
    0xFF0000, 0xFF4E00, 0xFF8A00, 0xFFC400, 0xC6FF00, 0x18FF62,
    0x00E5D0, 0x00A8FF, 0x3355FF, 0xB14BFF, 0xFFC98A, 0xFFFFFF,
};
constexpr size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void onPowerToggled(lv_event_t *e) {
  Leds::setOn(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

void onBrightnessChanged(lv_event_t *e) {
  Leds::setBrightness((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

void onModePressed(lv_event_t *e) {
  Leds::setMode((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

void onWheelChanged(lv_event_t *e) {
  const uint32_t rgb = lv_color_to32(lv_colorwheel_get_rgb(lv_event_get_target(e)));
  Leds::setColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void onPresetPressed(lv_event_t *e) {
  const uint32_t hex = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
  Leds::setColor((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

void onCalibratePressed(lv_event_t *) { calibrationRequested = true; }

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void buildTopBar(lv_obj_t *parent) {
  lv_obj_t *bar = slab(parent, 0, 0, SCREEN_W, TOPBAR_H);
  lv_obj_set_style_bg_color(bar, col(SURFACE), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

  lv_obj_t *wordmark = eyebrow(bar, "HELMET", PAD, 11);
  lv_obj_set_style_text_letter_space(wordmark, 3, 0);

  lv_obj_t *rule = slab(bar, 84, 12, 1, 12);
  lv_obj_set_style_bg_color(rule, col(LINE), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

  modeTitle = lv_label_create(bar);
  lv_label_set_text(modeTitle, "");
  lv_obj_set_style_text_font(modeTitle, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(modeTitle, col(TEXT), 0);
  lv_obj_set_pos(modeTitle, 96, 8);

  bleIcon = lv_label_create(bar);
  lv_label_set_text(bleIcon, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_font(bleIcon, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(bleIcon, col(MUTED), 0);
  lv_obj_align(bleIcon, LV_ALIGN_RIGHT_MID, -66, 0);

  powerSwitch = lv_switch_create(bar);
  lv_obj_set_size(powerSwitch, 46, 24);
  lv_obj_align(powerSwitch, LV_ALIGN_RIGHT_MID, -PAD, 0);
  lv_obj_set_style_bg_color(powerSwitch, col(CARD_HI), 0);
  lv_obj_set_style_bg_color(powerSwitch, col(LIVE), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(powerSwitch, lv_color_white(), LV_PART_KNOB);
  lv_obj_add_event_cb(powerSwitch, onPowerToggled, LV_EVENT_VALUE_CHANGED, nullptr);

  // Live PWM level, painted in the strip's own colour. Lets you see a pattern
  // running without looking up at the helmet.
  pulseBar = lv_bar_create(parent);
  lv_obj_remove_style_all(pulseBar);
  lv_obj_set_pos(pulseBar, 0, TOPBAR_H);
  lv_obj_set_size(pulseBar, SCREEN_W, PULSE_H);
  lv_bar_set_range(pulseBar, 0, 255);
  // The unlit part of the meter deliberately matches the top bar: only the lit
  // run should read, otherwise it looks like a second progress bar sitting
  // above the brightness slider.
  lv_obj_set_style_bg_color(pulseBar, col(SURFACE), 0);
  lv_obj_set_style_bg_opa(pulseBar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(pulseBar, LV_OPA_COVER, LV_PART_INDICATOR);
}

void buildBrightnessRow(lv_obj_t *parent) {
  lv_obj_t *row = slab(parent, 0, BRIGHT_TOP, SCREEN_W, BRIGHT_H);

  eyebrow(row, "BRIGHTNESS", PAD + 2, 3);

  briValue = lv_label_create(row);
  lv_label_set_text(briValue, "");
  lv_obj_set_style_text_font(briValue, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(briValue, col(TEXT), 0);
  lv_obj_align(briValue, LV_ALIGN_TOP_RIGHT, -(PAD + 2), 0);

  briSlider = lv_slider_create(row);
  lv_obj_remove_style_all(briSlider);
  lv_obj_set_pos(briSlider, PAD + 2, 24);
  lv_obj_set_size(briSlider, SCREEN_W - 2 * (PAD + 2), 8);
  lv_slider_set_range(briSlider, BRIGHTNESS_MIN, 255);
  lv_obj_set_ext_click_area(briSlider, 14);  // 8px of track is not a touch target

  lv_obj_set_style_radius(briSlider, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(briSlider, col(CARD_HI), 0);
  lv_obj_set_style_bg_opa(briSlider, LV_OPA_COVER, 0);

  lv_obj_set_style_radius(briSlider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(briSlider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(briSlider, col(ACCENT_DK), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(briSlider, col(ACCENT_LT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(briSlider, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

  lv_obj_set_style_radius(briSlider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(briSlider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_bg_color(briSlider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(briSlider, 5, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(briSlider, 10, LV_PART_KNOB);
  lv_obj_set_style_shadow_color(briSlider, col(ACCENT), LV_PART_KNOB);
  lv_obj_set_style_shadow_opa(briSlider, LV_OPA_50, LV_PART_KNOB);

  lv_obj_add_event_cb(briSlider, onBrightnessChanged, LV_EVENT_VALUE_CHANGED, nullptr);
}

void buildModesPage(lv_obj_t *page) {
  constexpr lv_coord_t cols = 3;
  constexpr lv_coord_t gap = 6;
  constexpr lv_coord_t cellW = (SCREEN_W - 2 * PAD - (cols - 1) * gap) / cols;
  constexpr lv_coord_t cellH = 36;

  for (int i = 0; i < MODE_COUNT; i++) {
    const lv_coord_t cx = PAD + (i % cols) * (cellW + gap);
    const lv_coord_t cy = 2 + (i / cols) * (cellH + gap);

    lv_obj_t *btn = lv_obj_create(page);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, cx, cy);
    lv_obj_set_size(btn, cellW, cellH);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(btn, &styleCard, 0);
    lv_obj_add_style(btn, &styleCardPressed, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &styleCardChecked, LV_STATE_CHECKED);
    lv_obj_add_event_cb(btn, onModePressed, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, Leds::modeNameAt((uint8_t)i));
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_center(label);

    modeBtn[i] = btn;
    modeBtnLabel[i] = label;
  }
}

void buildColorPage(lv_obj_t *page) {
  constexpr lv_coord_t wheelSize = 116;
  constexpr lv_coord_t wheelX = 8;
  constexpr lv_coord_t wheelY = 4;
  constexpr lv_coord_t dotSize = 74;

  wheel = lv_colorwheel_create(page, true);
  lv_obj_set_pos(wheel, wheelX, wheelY);
  lv_obj_set_size(wheel, wheelSize, wheelSize);
  lv_colorwheel_set_mode(wheel, LV_COLORWHEEL_MODE_HUE);
  // Long-press normally cycles hue/saturation/value rings — undiscoverable, and
  // the presets plus the brightness slider already cover those axes.
  lv_colorwheel_set_mode_fixed(wheel, true);
  lv_obj_set_style_arc_width(wheel, 16, LV_PART_MAIN);
  // Knob radius is arc_width/2 + knob padding; the stock theme's padding makes
  // it balloon well past the ring. 2px keeps it just proud of the track.
  lv_obj_set_style_pad_all(wheel, 2, LV_PART_KNOB);
  lv_obj_set_style_border_width(wheel, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(wheel, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_border_opa(wheel, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_add_event_cb(wheel, onWheelChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  // Current colour, shown in the hole of the ring. Not clickable, so presses
  // land on the wheel underneath.
  wheelDot = lv_obj_create(page);
  lv_obj_remove_style_all(wheelDot);
  lv_obj_set_pos(wheelDot, wheelX + (wheelSize - dotSize) / 2,
                 wheelY + (wheelSize - dotSize) / 2);
  lv_obj_set_size(wheelDot, dotSize, dotSize);
  lv_obj_clear_flag(wheelDot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(wheelDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(wheelDot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(wheelDot, 2, 0);
  lv_obj_set_style_border_color(wheelDot, col(BG), 0);
  lv_obj_set_style_border_opa(wheelDot, LV_OPA_COVER, 0);

  wheelHex = lv_label_create(wheelDot);
  lv_label_set_text(wheelHex, "");
  lv_obj_set_style_text_font(wheelHex, &lv_font_montserrat_12, 0);
  lv_obj_center(wheelHex);

  // Preset swatches, 4 x 3, filling the right half.
  constexpr lv_coord_t px0 = 134;
  constexpr lv_coord_t swW = 40;
  constexpr lv_coord_t swH = 34;
  constexpr lv_coord_t swGap = 6;
  for (size_t i = 0; i < kPresetCount; i++) {
    const lv_coord_t sx = px0 + (lv_coord_t)(i % 4) * (swW + swGap);
    const lv_coord_t sy = 6 + (lv_coord_t)(i / 4) * (swH + swGap);

    lv_obj_t *sw = lv_obj_create(page);
    lv_obj_remove_style_all(sw);
    lv_obj_set_pos(sw, sx, sy);
    lv_obj_set_size(sw, swW, swH);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(sw, 8, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, col(kPresets[i]), 0);
    lv_obj_set_style_border_width(sw, 1, 0);
    lv_obj_set_style_border_color(sw, col(LINE), 0);
    lv_obj_set_style_border_opa(sw, LV_OPA_COVER, 0);
    // Press feedback that works on any hue: a bright ring, not a colour shift.
    lv_obj_set_style_outline_width(sw, 2, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(sw, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(sw, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_event_cb(sw, onPresetPressed, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)kPresets[i]);
  }
}

// One "Label ........ value" row plus the hairline under it.
lv_obj_t *buildInfoRow(lv_obj_t *page, const char *label, const char *value,
                       lv_coord_t y, bool separator) {
  lv_obj_t *l = lv_label_create(page);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(l, col(MUTED), 0);
  lv_obj_set_pos(l, PAD + 2, y);

  lv_obj_t *v = lv_label_create(page);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(v, col(TEXT_DIM), 0);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -(PAD + 2), y);

  if (separator) {
    lv_obj_t *line = slab(page, PAD + 2, y + 19, SCREEN_W - 2 * (PAD + 2), 1);
    lv_obj_set_style_bg_color(line, col(LINE), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  }
  return v;
}

void buildSystemPage(lv_obj_t *page) {
  char hw[40];
  snprintf(hw, sizeof(hw), "%d px  " LV_SYMBOL_BULLET "  GPIO%d", LED_COUNT,
           PIN_LED_DATA);

  buildInfoRow(page, "Firmware", "v" HELMET_FW_VERSION, 2, true);
  buildInfoRow(page, "LED strip", hw, 26, true);
  sysBleValue = buildInfoRow(page, "Bluetooth", "Advertising", 50, true);
  sysUptimeValue = buildInfoRow(page, "Uptime", "0m 00s", 74, false);

  lv_obj_t *btn = lv_obj_create(page);
  lv_obj_remove_style_all(btn);
  lv_obj_set_pos(btn, PAD, 96);
  lv_obj_set_size(btn, SCREEN_W - 2 * PAD, 26);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(btn, &styleCard, 0);
  lv_obj_add_style(btn, &styleCardPressed, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn, onCalibratePressed, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_GPS "  Calibrate touchscreen");
  lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_center(l);
}

void styleTabBar(lv_obj_t *tv) {
  lv_obj_t *btns = lv_tabview_get_tab_btns(tv);
  lv_obj_set_style_bg_color(btns, col(SURFACE), 0);
  lv_obj_set_style_bg_opa(btns, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(btns, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(btns, 1, 0);
  lv_obj_set_style_border_color(btns, col(LINE), 0);
  lv_obj_set_style_border_opa(btns, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(btns, 0, 0);
  lv_obj_set_style_text_font(btns, &lv_font_montserrat_12, 0);

  lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_border_width(btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(btns, col(MUTED), LV_PART_ITEMS);

  lv_obj_set_style_bg_color(btns, col(CARD), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(btns, col(ACCENT_LT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_side(btns, LV_BORDER_SIDE_TOP,
                               LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(btns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(btns, col(ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_opa(btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);

  lv_obj_set_style_bg_color(btns, col(CARD_HI), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
}

void preparePage(lv_obj_t *page) {
  lv_obj_set_style_pad_all(page, 0, 0);
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

// ---------------------------------------------------------------------------
// Splash
// ---------------------------------------------------------------------------

void splashFadeCb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void splashDoneCb(lv_anim_t *a) { lv_obj_del((lv_obj_t *)a->var); }

// ---------------------------------------------------------------------------
// State sync
// ---------------------------------------------------------------------------

void syncColor(uint8_t r, uint8_t g, uint8_t b) {
  const lv_color_t c = lv_color_make(r, g, b);
  lv_obj_set_style_bg_color(pulseBar, c, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(wheelDot, c, 0);

  char hex[10];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
  lv_label_set_text(wheelHex, hex);
  lv_obj_set_style_text_color(wheelHex, contrastOn(r, g, b), 0);

  // The wheel only carries hue, and round-tripping through 16-bit colour drifts
  // a little, so only push back when the change did not come from the wheel.
  if (!lv_obj_has_state(wheel, LV_STATE_PRESSED)) {
    lv_colorwheel_set_rgb(wheel, c);
  }
}

}  // namespace

void build() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, col(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar(scr);
  buildBrightnessRow(scr);

  tabview = lv_tabview_create(scr, LV_DIR_BOTTOM, TABBAR_H);
  lv_obj_set_pos(tabview, 0, TABVIEW_TOP);
  lv_obj_set_size(tabview, SCREEN_W, SCREEN_H - TABVIEW_TOP);
  lv_obj_set_style_bg_color(tabview, col(BG), 0);
  lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
  styleTabBar(tabview);

  lv_obj_t *content = lv_tabview_get_content(tabview);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

  lv_obj_t *modesPage = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST "  Modes");
  lv_obj_t *colorPage = lv_tabview_add_tab(tabview, LV_SYMBOL_TINT "  Colour");
  lv_obj_t *systemPage = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS "  System");
  preparePage(modesPage);
  preparePage(colorPage);
  preparePage(systemPage);

  buildModesPage(modesPage);
  buildColorPage(colorPage);
  buildSystemPage(systemPage);
}

void showSplash(uint32_t holdMs) {
  lv_obj_t *splash = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(splash);
  lv_obj_set_pos(splash, 0, 0);
  lv_obj_set_size(splash, SCREEN_W, SCREEN_H);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(splash, LV_OBJ_FLAG_CLICKABLE);  // swallow stray touches
  lv_obj_set_style_bg_color(splash, col(BG), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);

  lv_obj_t *title = lv_label_create(splash);
  lv_label_set_text(title, "HELMET");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(title, col(TEXT), 0);
  lv_obj_set_style_text_letter_space(title, 6, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 3, -18);  // optical centring vs. tracking

  lv_obj_t *rule = slab(splash, 0, 0, 72, 2);
  lv_obj_set_style_bg_color(rule, col(ACCENT), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(rule, 1, 0);
  lv_obj_align(rule, LV_ALIGN_CENTER, 0, 8);

  lv_obj_t *ver = lv_label_create(splash);
  lv_label_set_text(ver, "LED CONTROLLER  " LV_SYMBOL_BULLET "  v" HELMET_FW_VERSION);
  lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(ver, col(MUTED), 0);
  lv_obj_set_style_text_letter_space(ver, 2, 0);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, 30);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, splash);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_exec_cb(&a, splashFadeCb);
  lv_anim_set_ready_cb(&a, splashDoneCb);
  lv_anim_set_time(&a, 320);
  lv_anim_set_delay(&a, holdMs);
  lv_anim_start(&a);
}

void sync(uint32_t uptimeSec) {
  const uint8_t mode = Leds::mode();
  if (mode != shownMode) {
    for (int i = 0; i < MODE_COUNT; i++) {
      const bool active = (i == mode);
      if (active) {
        lv_obj_add_state(modeBtn[i], LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(modeBtn[i], LV_STATE_CHECKED);
      }
      lv_obj_set_style_text_color(modeBtnLabel[i],
                                  active ? lv_color_white() : col(TEXT_DIM), 0);
    }
    lv_label_set_text(modeTitle, Leds::modeName());
    shownMode = mode;
  }

  const bool on = Leds::on();
  if (on != shownOn) {
    if (on) {
      lv_obj_add_state(powerSwitch, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(powerSwitch, LV_STATE_CHECKED);
    }
    shownOn = on;
  }

  const uint8_t bri = Leds::brightness();
  if (bri != shownBri) {
    if (!lv_obj_has_state(briSlider, LV_STATE_PRESSED)) {
      lv_slider_set_value(briSlider, bri, LV_ANIM_OFF);
    }
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", Leds::brightnessPercent());
    lv_label_set_text(briValue, pct);
    shownBri = bri;
  }

  uint8_t r, g, b;
  Leds::color(&r, &g, &b);
  if (!shownColorValid || r != shownR || g != shownG || b != shownB) {
    syncColor(r, g, b);
    shownR = r;
    shownG = g;
    shownB = b;
    shownColorValid = true;
  }

  lv_bar_set_value(pulseBar, on ? Leds::outputLevel() : 0, LV_ANIM_OFF);

  const bool ble = Ble::connected();
  if (!shownBleValid || ble != shownBle) {
    lv_obj_set_style_text_color(bleIcon, ble ? col(ACCENT_LT) : col(MUTED), 0);
    lv_label_set_text(sysBleValue, ble ? "Connected" : "Advertising");
    shownBle = ble;
    shownBleValid = true;
  }

  if (uptimeSec != shownUptimeSec) {
    char buf[20];
    if (uptimeSec >= 3600) {
      snprintf(buf, sizeof(buf), "%uh %02um", (unsigned)(uptimeSec / 3600),
               (unsigned)((uptimeSec % 3600) / 60));
    } else {
      snprintf(buf, sizeof(buf), "%um %02us", (unsigned)(uptimeSec / 60),
               (unsigned)(uptimeSec % 60));
    }
    lv_label_set_text(sysUptimeValue, buf);
    shownUptimeSec = uptimeSec;
  }
}

bool takeCalibrationRequest() {
  const bool wanted = calibrationRequested;
  calibrationRequested = false;
  return wanted;
}

void showTab(uint8_t index) {
  if (tabview != nullptr) {
    lv_tabview_set_act(tabview, index, LV_ANIM_OFF);
  }
}

}  // namespace UiScreens
