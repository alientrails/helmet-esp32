#include "ui_screens.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ble.h"
#include "config.h"
#include "leds.h"
#include "ui_theme.h"

// Layout, 320x240 landscape, Alien Trails Labs Void theme:
//   0..38    top bar    — mark, running mode, BLE signal tag, power switch
//   38..40   pulse      — live PWM output, painted in the strip's own colour
//   40..84   controls   — brightness (the trail) and speed, side by side
//   84..206  tab pages  — Modes (3 across) / Colour (ring + presets) /
//                         Bass (meter + tuning) / Strip (wiring + power) /
//                         System
//   206..240 tab bar    — MODES · COLOUR · BASS · STRIP · SYSTEM

namespace UiScreens {
namespace {

using namespace UiTheme;

lv_obj_t *tabview = nullptr;
lv_obj_t *modeTitle = nullptr;
lv_obj_t *linkTag = nullptr;
lv_obj_t *linkPip = nullptr;
lv_obj_t *linkLabel = nullptr;
lv_obj_t *powerSwitch = nullptr;
lv_obj_t *pulseBar = nullptr;
lv_obj_t *briSlider = nullptr;
lv_obj_t *briValue = nullptr;
lv_obj_t *speedSlider = nullptr;
lv_obj_t *speedValue = nullptr;
lv_obj_t *modeBtn[MODE_COUNT] = {nullptr};
lv_obj_t *wheel = nullptr;
lv_obj_t *wheelDot = nullptr;
lv_obj_t *wheelHex = nullptr;
lv_obj_t *orderBtn[ORDER_COUNT] = {nullptr};
lv_obj_t *orderBtnLabel[ORDER_COUNT] = {nullptr};
lv_obj_t *countSlider = nullptr;
lv_obj_t *countValue = nullptr;
lv_obj_t *maxMaSlider = nullptr;
lv_obj_t *maxMaValue = nullptr;
lv_obj_t *skipSlider = nullptr;
lv_obj_t *skipValue = nullptr;

// Bass page. The tab index is fixed by the order in build().
constexpr uint16_t kBassTab = 2;
lv_obj_t *bassMeterObj = nullptr;
lv_obj_t *bassMeterHint = nullptr;
lv_obj_t *bassReading = nullptr;
lv_obj_t *bassKickPip = nullptr;
lv_obj_t *bassSlider[4] = {nullptr};  // indexed by BassParam 0..3
lv_obj_t *bassValue[4] = {nullptr};
lv_obj_t *bandBtn[BassDsp::BAND_COUNT] = {nullptr};
lv_obj_t *bandLabel[BassDsp::BAND_COUNT] = {nullptr};
lv_obj_t *styleBtn[BASS_STYLE_COUNT] = {nullptr};
lv_obj_t *styleLabel[BASS_STYLE_COUNT] = {nullptr};
lv_obj_t *colourBtn[BASS_COLOUR_COUNT] = {nullptr};  // violet, mine, rainbow
lv_obj_t *colourLabel[BASS_COLOUR_COUNT] = {nullptr};
lv_obj_t *flashBtn = nullptr;
lv_obj_t *flashLabel = nullptr;
lv_obj_t *humBtn = nullptr;
lv_obj_t *humLabel = nullptr;
lv_obj_t *sysBleValue = nullptr;
lv_obj_t *sysUptimeValue = nullptr;

bool calibrationRequested = false;

// Mirrors of the LED state, so the refresh only touches LVGL (and so only
// repaints) when something actually moved. BLE and serial can change the same
// state behind our back, which is exactly what this catches.
uint8_t shownMode = 0xFF;
uint8_t shownBri = 0xFF;
uint8_t shownSpeed = 0xFF;
uint8_t shownOrder = 0xFF;
uint16_t shownCount = 0xFFFF;
uint16_t shownMaxMa = 0xFFFF;
uint16_t shownSkip = 0xFFFF;
uint16_t shownLit = 0xFFFF;
// Filled with "never shown" in begin-of-build, so every control paints on the
// first sync whatever its value -- a literal initialiser here silently left
// any newer BassParam at 0, which reads as "already shown".
uint16_t shownBass[BASS_PARAM_COUNT];

// The meter repaints on its own fast timer, not in sync(), so these are its
// own mirror of the detector.
bool meterLive = false;
bool meterLiveValid = false;
uint8_t meterLevel = 0;
uint8_t meterTrigger = 0;
uint8_t meterGate = 0;
uint8_t meterOut = 0;
uint8_t meterKicks = 0;
bool meterKicksValid = false;
uint32_t meterKickAt = 0;
bool meterKickLit = false;
bool shownOn = false;
uint8_t shownR = 0, shownG = 0, shownB = 0;
bool shownColorValid = false;
bool shownBle = false;
bool shownBleValid = false;
uint32_t shownUptimeSec = 0xFFFFFFFF;

// A 12-swatch palette spanning the useful gamut, including a warm and a neutral
// white — the two things a hue ring alone cannot give you. These are LED
// colours, not UI chrome, so they sit outside the brand palette on purpose.
constexpr uint32_t kPresets[] = {
    0xFF0000, 0xFF4E00, 0xFF8A00, 0xFFC400, 0xC6FF00, 0x18FF62,
    0x00E5D0, 0x00A8FF, 0x3355FF, 0xB14BFF, 0xFFC98A, 0xFFFFFF,
};
constexpr size_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

// Trail slider geometry: dots on a space-2 pitch, like the TrailRule.
constexpr lv_coord_t kTrailPitch = 8;
constexpr lv_coord_t kTrailDotLit = 4;
constexpr lv_coord_t kTrailDotDim = 2;

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void onPowerToggled(lv_event_t *e) {
  Leds::setOn(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

void onBrightnessChanged(lv_event_t *e) {
  Leds::setBrightness((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

void onSpeedChanged(lv_event_t *e) {
  Leds::setSpeed((uint8_t)lv_slider_get_value(lv_event_get_target(e)));
}

void onOrderPressed(lv_event_t *e) {
  Leds::setOrder((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

void onCountChanged(lv_event_t *e) {
  Leds::setCount((uint16_t)lv_slider_get_value(lv_event_get_target(e)));
}

void onSkipChanged(lv_event_t *e) {
  Leds::setSkip((uint16_t)lv_slider_get_value(lv_event_get_target(e)));
}

// One handler for the four tuning sliders; each carries its BassParam id.
void onBassSlider(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  Leds::setBass((uint8_t)(uintptr_t)lv_obj_get_user_data(slider),
                (uint8_t)lv_slider_get_value(slider));
}

void onBandPressed(lv_event_t *e) {
  Leds::setBass(BASS_BAND, (uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

void onStylePressed(lv_event_t *e) {
  Leds::setBass(BASS_STYLE, (uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

void onColourPressed(lv_event_t *e) {
  const uint8_t choice = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  Leds::setBass(BASS_FLAGS, withBassColour(Leds::bass(BASS_FLAGS), choice));
}

void onFlashPressed(lv_event_t *) {
  Leds::setBass(BASS_FLAGS, Leds::bass(BASS_FLAGS) ^ BASS_FLAG_FLASH);
}

void onHumPressed(lv_event_t *) { Leds::setBass(BASS_HUM, Leds::bass(BASS_HUM) ? 0 : 1); }

void onMaxMaChanged(lv_event_t *e) {
  // Snapped to 50 mA: the slider is 1900 mA wide over 296 px, so raw values
  // would jitter by tens of milliamps between repaints and read as noise.
  const int32_t v = lv_slider_get_value(lv_event_get_target(e));
  Leds::setMaxMilliamps((uint16_t)((v / 50) * 50));
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

// Paints the brightness track as the brand's trail: a row of dots that fades
// phosphor to ion up to the knob, and dim line-strong dots past it. Drawn
// before the slider's own parts, so the ion knob sits on top as the trail's
// end dot.
void onTrailDraw(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);

  lv_area_t box;
  lv_obj_get_coords(obj, &box);
  const lv_coord_t litEnd = ((lv_slider_t *)obj)->bar.indic_area.x2;
  const lv_coord_t cy = (box.y1 + box.y2) / 2;
  const lv_coord_t span = lv_area_get_width(&box);

  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.bg_opa = LV_OPA_COVER;

  for (lv_coord_t x = box.x1 + kTrailPitch / 2; x <= box.x2; x += kTrailPitch) {
    const bool lit = x <= litEnd;
    const lv_coord_t d = lit ? kTrailDotLit : kTrailDotDim;
    if (lit) {
      const uint8_t t = (uint8_t)(((x - box.x1) * 255) / span);
      dot.bg_color = lv_color_mix(col(ION), col(PHOSPHOR), t);
    } else {
      dot.bg_color = col(LINE_STRONG);
    }
    lv_area_t a = {(lv_coord_t)(x - d / 2), (lv_coord_t)(cy - d / 2),
                   (lv_coord_t)(x - d / 2 + d - 1), (lv_coord_t)(cy - d / 2 + d - 1)};
    lv_draw_rect(ctx, &dot, &a);
  }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// The SignalTag from the design system: a mono UPPERCASE word with a pip
// whose shape carries the meaning, so the state reads without colour.
void buildLinkTag(lv_obj_t *parent) {
  linkTag = lv_obj_create(parent);
  lv_obj_remove_style_all(linkTag);
  lv_obj_clear_flag(linkTag, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(linkTag, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_radius(linkTag, RADIUS_SM, 0);
  lv_obj_set_style_bg_opa(linkTag, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(linkTag, 6, 0);
  lv_obj_set_flex_flow(linkTag, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(linkTag, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(linkTag, 5, 0);

  linkPip = lv_obj_create(linkTag);
  lv_obj_remove_style_all(linkPip);
  lv_obj_set_size(linkPip, 7, 7);
  lv_obj_set_style_radius(linkPip, LV_RADIUS_CIRCLE, 0);

  linkLabel = lv_label_create(linkTag);
  lv_obj_set_style_text_font(linkLabel, &at_mono_11, 0);
  lv_obj_set_style_text_letter_space(linkLabel, 1, 0);
}

void buildTopBar(lv_obj_t *parent) {
  lv_obj_t *bar = slab(parent, 0, 0, SCREEN_W, TOPBAR_H);
  lv_obj_set_style_bg_color(bar, col(HULL), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

  // The mark as a maker's stamp, at the style guide's 24px minimum.
  lv_obj_t *mark = lv_img_create(bar);
  lv_img_set_src(mark, &at_img_mark_bar);
  lv_obj_set_pos(mark, PAD, (TOPBAR_H - 24) / 2);

  hairline(bar, PAD + 19 + SPACE_3, 11, 1, 16);

  modeTitle = lv_label_create(bar);
  lv_label_set_text(modeTitle, "");
  lv_obj_set_style_text_font(modeTitle, &at_sans_20, 0);
  lv_obj_set_style_text_color(modeTitle, col(INK), 0);
  lv_obj_set_pos(modeTitle, PAD + 19 + 2 * SPACE_3 + 1, 8);

  powerSwitch = lv_switch_create(bar);
  lv_obj_set_size(powerSwitch, 42, 22);
  lv_obj_align(powerSwitch, LV_ALIGN_RIGHT_MID, -PAD, 0);
  lv_obj_set_style_bg_color(powerSwitch, col(HULL_HIGH), 0);
  lv_obj_set_style_bg_opa(powerSwitch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(powerSwitch, 1, 0);
  lv_obj_set_style_border_color(powerSwitch, col(LINE_STRONG), 0);
  lv_obj_set_style_border_opa(powerSwitch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(powerSwitch, col(PHOSPHOR), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(powerSwitch, col(PHOSPHOR),
                            LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(powerSwitch, col(INK_MUTED), LV_PART_KNOB);
  lv_obj_set_style_bg_color(powerSwitch, col(ON_PHOSPHOR),
                            LV_PART_KNOB | LV_STATE_CHECKED);
  lv_obj_set_style_pad_all(powerSwitch, -3, LV_PART_KNOB);
  lv_obj_add_event_cb(powerSwitch, onPowerToggled, LV_EVENT_VALUE_CHANGED, nullptr);

  buildLinkTag(bar);
  lv_obj_align(linkTag, LV_ALIGN_RIGHT_MID, -(PAD + 42 + SPACE_3), 0);

  // Live PWM level, painted in the strip's own colour. Lets you see a pattern
  // running without looking up at the helmet. Unlit, it is just the bar's
  // bottom hairline.
  pulseBar = lv_bar_create(parent);
  lv_obj_remove_style_all(pulseBar);
  lv_obj_set_pos(pulseBar, 0, TOPBAR_H);
  lv_obj_set_size(pulseBar, SCREEN_W, PULSE_H);
  lv_bar_set_range(pulseBar, 0, 255);
  lv_obj_set_style_bg_color(pulseBar, col(LINE), 0);
  lv_obj_set_style_bg_opa(pulseBar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(pulseBar, LV_OPA_COVER, LV_PART_INDICATOR);
}

// A labelled slider: eyebrow on the left, live value on the right, track
// underneath. Brightness, speed, pixel count and the current budget are all the
// same control at different scales, so they are all built from this.
//
// `trail` swaps the plain track for the brand's dotted phosphor-to-ion trail.
// House rule 3 spends that once per view, so brightness gets it and every other
// slider stays a quiet ion bar.
constexpr lv_coord_t SLIDER_BLOCK_H = 38;

lv_obj_t *buildSliderBlock(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, const char *title, int32_t lo, int32_t hi,
                           lv_event_cb_t cb, lv_obj_t **valueOut,
                           bool trail = false) {
  lv_obj_t *block = slab(parent, x, y, w, SLIDER_BLOCK_H);

  eyebrow(block, title, 0, 3);

  // Readings are instrument output, so Mono.
  lv_obj_t *value = lv_label_create(block);
  lv_label_set_text(value, "");
  lv_obj_set_style_text_font(value, &at_mono_13, 0);
  lv_obj_set_style_text_color(value, col(INK), 0);
  lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, 0);
  *valueOut = value;

  lv_obj_t *slider = lv_slider_create(block);
  lv_obj_remove_style_all(slider);
  lv_obj_set_pos(slider, 0, 24);
  lv_obj_set_size(slider, w, trail ? 12 : 8);
  lv_slider_set_range(slider, lo, hi);
  lv_obj_set_ext_click_area(slider, 14);  // a thin track is not a touch target

  if (trail) {
    // The trail paints its own track, so both bar parts stay transparent.
    lv_obj_add_event_cb(slider, onTrailDraw, LV_EVENT_DRAW_MAIN_BEGIN, nullptr);
  } else {
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(slider, col(LINE_STRONG), 0);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, 0);

    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, col(ION), LV_PART_INDICATOR);
  }

  // Knob: ion, circular. The ion glow is kept for the trail, because a glow is
  // light leaking from something live and brightness is the only one that is.
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_bg_color(slider, col(ION), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, trail ? 1 : 4, LV_PART_KNOB);
  if (trail) {
    lv_obj_set_style_shadow_width(slider, 14, LV_PART_KNOB);
    lv_obj_set_style_shadow_spread(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, col(ION), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_50, LV_PART_KNOB);
  }
  lv_obj_set_style_outline_width(slider, 2, LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_outline_pad(slider, 2, LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_outline_color(slider, col(ION), LV_PART_KNOB | LV_STATE_PRESSED);
  lv_obj_set_style_outline_opa(slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_PRESSED);

  lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return slider;
}

// Brightness and speed share the header row. Both are adjusted mid-ride with
// the helmet on someone's head, so they stay one touch away on every tab
// rather than living behind the Modes grid.
void buildControlsRow(lv_obj_t *parent) {
  lv_obj_t *row = slab(parent, 0, BRIGHT_TOP, SCREEN_W, BRIGHT_H);

  constexpr lv_coord_t inset = PAD + 2;
  constexpr lv_coord_t colGap = 14;
  constexpr lv_coord_t colW = (SCREEN_W - 2 * inset - colGap) / 2;

  briSlider = buildSliderBlock(row, inset, 0, colW, "BRIGHTNESS", BRIGHTNESS_MIN,
                               255, onBrightnessChanged, &briValue, /*trail=*/true);
  speedSlider = buildSliderBlock(row, inset + colW + colGap, 0, colW, "SPEED",
                                 SPEED_MIN, 255, onSpeedChanged, &speedValue);
}

// Turns a tab page into a vertical scroller with the brand's thin ion bar.
// Vertical only: a sideways drag on a resistive panel is too easy to produce
// by accident while aiming for a control. preparePage() clears the flag, so
// this re-arms it for the pages whose content outgrows PAGE_H.
void makeScrollable(lv_obj_t *page, lv_coord_t padBottom) {
  lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(page, LV_DIR_VER);
  lv_obj_set_style_pad_bottom(page, padBottom, 0);

  // Always shown rather than on-scroll: in a window this short the bar is the
  // only standing cue that there is more below.
  lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_width(page, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(page, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(page, LV_RADIUS_CIRCLE, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(page, col(ION), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(page, LV_OPA_40, LV_PART_SCROLLBAR);
}

void buildModesPage(lv_obj_t *page) {
  // Sixteen modes, three to a row. Three columns rather than four because the
  // width was the tight dimension, not the height: "Heartbeat" and "Slow
  // blink" ran to the cell edges at 69px. At 94x40 a gloved thumb has a real
  // target, which is worth more here than seeing every mode at once.
  constexpr lv_coord_t cols = 3;
  constexpr lv_coord_t gap = 6;
  constexpr lv_coord_t top = 2;
  constexpr lv_coord_t rowGap = 6;
  constexpr lv_coord_t cellW = (SCREEN_W - 2 * PAD - (cols - 1) * gap) / cols;
  constexpr lv_coord_t cellH = 40;
  constexpr int rows = (MODE_COUNT + cols - 1) / cols;
  constexpr lv_coord_t gridH = top + rows * cellH + (rows - 1) * rowGap;

  // The longest mode name at at_sans_14 needs about 80px inside the cell.
  static_assert(cellW >= 90, "mode cells too narrow for the longest mode name");
  // If this ever fails the grid fits again and the scrolling below is dead weight.
  static_assert(gridH > PAGE_H, "modes grid now fits the page; drop the scrolling");

  makeScrollable(page, top);

  for (int i = 0; i < MODE_COUNT; i++) {
    const lv_coord_t cx = PAD + (i % cols) * (cellW + gap);
    const lv_coord_t cy = top + (i / cols) * (cellH + rowGap);

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
    lv_obj_center(label);

    modeBtn[i] = btn;
  }
}

void buildColorPage(lv_obj_t *page) {
  constexpr lv_coord_t wheelSize = 112;
  constexpr lv_coord_t wheelX = PAD;
  constexpr lv_coord_t wheelY = (PAGE_H - wheelSize) / 2;
  constexpr lv_coord_t dotSize = 70;

  wheel = lv_colorwheel_create(page, true);
  lv_obj_set_pos(wheel, wheelX, wheelY);
  lv_obj_set_size(wheel, wheelSize, wheelSize);
  lv_colorwheel_set_mode(wheel, LV_COLORWHEEL_MODE_HUE);
  // Long-press normally cycles hue/saturation/value rings — undiscoverable, and
  // the presets plus the brightness slider already cover those axes.
  lv_colorwheel_set_mode_fixed(wheel, true);
  lv_obj_set_style_arc_width(wheel, 14, LV_PART_MAIN);
  // Knob radius is arc_width/2 + knob padding; the stock theme's padding makes
  // it balloon well past the ring. 2px keeps it just proud of the track.
  lv_obj_set_style_pad_all(wheel, 2, LV_PART_KNOB);
  lv_obj_set_style_border_width(wheel, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(wheel, col(INK), LV_PART_KNOB);
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
  lv_obj_set_style_border_width(wheelDot, 3, 0);
  lv_obj_set_style_border_color(wheelDot, col(VOID), 0);
  lv_obj_set_style_border_opa(wheelDot, LV_OPA_COVER, 0);

  wheelHex = lv_label_create(wheelDot);
  lv_label_set_text(wheelHex, "");
  lv_obj_set_style_text_font(wheelHex, &at_mono_11, 0);
  lv_obj_center(wheelHex);

  // Preset swatches, 4 x 3, filling the right half under a PRESETS label.
  constexpr lv_coord_t px0 = 146;
  constexpr lv_coord_t swGap = 6;
  constexpr lv_coord_t swW = (SCREEN_W - PAD - px0 - 3 * swGap) / 4;
  constexpr lv_coord_t swTop = 26;
  constexpr lv_coord_t swH = (PAGE_H - swTop - 8 - 2 * swGap) / 3;
  eyebrow(page, "PRESETS", px0, 7);
  for (size_t i = 0; i < kPresetCount; i++) {
    const lv_coord_t sx = px0 + (lv_coord_t)(i % 4) * (swW + swGap);
    const lv_coord_t sy = swTop + (lv_coord_t)(i / 4) * (swH + swGap);

    lv_obj_t *sw = lv_obj_create(page);
    lv_obj_remove_style_all(sw);
    lv_obj_set_pos(sw, sx, sy);
    lv_obj_set_size(sw, swW, swH);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(sw, RADIUS_MD, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, col(kPresets[i]), 0);
    // Press feedback that works on any hue: the brand's focus ring, a ground
    // gap then 2px of solid ion.
    lv_obj_set_style_outline_width(sw, 2, LV_STATE_PRESSED);
    lv_obj_set_style_outline_pad(sw, 2, LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(sw, col(ION), LV_STATE_PRESSED);
    lv_obj_set_style_outline_opa(sw, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(sw, onPresetPressed, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)kPresets[i]);
  }
}

// The strip's own wiring and power budget. These used to be #defines that
// needed a reflash; a re-cut strip or a clone reel with a different channel
// order is exactly the sort of thing you discover once the helmet is already
// assembled.
void buildStripPage(lv_obj_t *page) {
  eyebrow(page, "COLOUR ORDER", PAD + 2, 0);

  constexpr lv_coord_t swGap = 4;
  constexpr lv_coord_t swW =
      (SCREEN_W - 2 * PAD - (ORDER_COUNT - 1) * swGap) / ORDER_COUNT;
  constexpr lv_coord_t swH = 26;
  constexpr lv_coord_t swX0 =
      (SCREEN_W - (ORDER_COUNT * swW + (ORDER_COUNT - 1) * swGap)) / 2;

  for (int i = 0; i < ORDER_COUNT; i++) {
    lv_obj_t *btn = lv_obj_create(page);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, swX0 + i * (swW + swGap), 16);
    lv_obj_set_size(btn, swW, swH);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_style(btn, &styleCard, 0);
    lv_obj_add_style(btn, &styleCardPressed, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &styleCardChecked, LV_STATE_CHECKED);
    lv_obj_add_event_cb(btn, onOrderPressed, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, Leds::orderNameAt((uint8_t)i));
    lv_obj_set_style_text_font(label, &at_mono_11, 0);
    lv_obj_center(label);

    orderBtn[i] = btn;
    orderBtnLabel[i] = label;
  }

  constexpr lv_coord_t inset = PAD + 2;
  constexpr lv_coord_t blockW = SCREEN_W - 2 * inset;

  // HIDDEN sits with PIXELS because the two together describe the geometry of
  // the strip; the current budget is a separate concern and goes last.
  countSlider = buildSliderBlock(page, inset, 48, blockW, "PIXELS", 1,
                                 LED_COUNT_MAX, onCountChanged, &countValue);
  skipSlider = buildSliderBlock(page, inset, 86, blockW, "HIDDEN", 0,
                                LED_SKIP_MAX, onSkipChanged, &skipValue);
  maxMaSlider =
      buildSliderBlock(page, inset, 124, blockW, "CURRENT BUDGET", LED_MAX_MA_MIN,
                       LED_MAX_MA_MAX, onMaxMaChanged, &maxMaValue);

  // Three slider blocks and the order row no longer fit in PAGE_H.
  makeScrollable(page, 8);
}

// The input meter: the detector's envelope as a bar, with the noise gate and
// the live kick threshold marked on it. It is what makes tuning possible at
// all -- with the mic covered you can see exactly where the noise sits and put
// the gate above it.
//
// The two marks differ in shape as well as colour, so they read without
// colour: the gate is a solid full-height line, the trigger a pair of notches.
void onMeterDraw(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t box;
  lv_obj_get_coords(obj, &box);

  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = RADIUS_SM;
  d.bg_color = col(HULL);
  d.bg_opa = LV_OPA_COVER;
  d.border_color = col(LINE_STRONG);
  d.border_width = 1;
  d.border_opa = LV_OPA_COVER;
  lv_draw_rect(ctx, &d, &box);
  if (!meterLive) {
    return;
  }

  const lv_coord_t x0 = box.x1 + 2;
  const lv_coord_t span = lv_area_get_width(&box) - 4;
  auto at = [&](uint8_t v) { return (lv_coord_t)(x0 + ((int32_t)span * v) / 255); };

  // The level: ion, or phosphor for the instant a kick fires. Phosphor marks
  // what is live, and a firing kick is the one live thing on this page.
  lv_draw_rect_dsc_t f;
  lv_draw_rect_dsc_init(&f);
  f.radius = 1;
  f.bg_opa = LV_OPA_COVER;
  f.bg_color = meterKickLit ? col(PHOSPHOR) : col(ION);
  lv_area_t fill = {x0, (lv_coord_t)(box.y1 + 2), at(meterLevel), (lv_coord_t)(box.y2 - 2)};
  if (fill.x2 > fill.x1) {
    lv_draw_rect(ctx, &f, &fill);
  }

  lv_draw_rect_dsc_t m;
  lv_draw_rect_dsc_init(&m);
  m.bg_opa = LV_OPA_COVER;

  // Trigger: two ink notches, top and bottom. Drawn first so the gate wins
  // where they coincide -- which is exactly the case in silence.
  const lv_coord_t tx = at(meterTrigger);
  m.bg_color = col(INK);
  lv_area_t top = {(lv_coord_t)(tx - 1), box.y1, (lv_coord_t)(tx + 1), (lv_coord_t)(box.y1 + 4)};
  lv_area_t bot = {(lv_coord_t)(tx - 1), (lv_coord_t)(box.y2 - 4), (lv_coord_t)(tx + 1), box.y2};
  lv_draw_rect(ctx, &m, &top);
  lv_draw_rect(ctx, &m, &bot);

  // Gate: a solid amber line through the whole bar.
  const lv_coord_t gx = at(meterGate);
  m.bg_color = col(AMBER);
  lv_area_t gate = {(lv_coord_t)(gx - 1), box.y1, gx, box.y2};
  lv_draw_rect(ctx, &m, &gate);
}

void setMeterHint(bool live) {
  if (live) {
    lv_obj_add_flag(bassMeterHint, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(bassMeterHint, LV_OBJ_FLAG_HIDDEN);
  }
}

// 30 Hz, and only while the Bass tab is on screen: sync() at 12 Hz is too slow
// to show a 100 ms kick, and nobody needs the meter painted when it is hidden.
void meterTimerCb(lv_timer_t *) {
  if (tabview == nullptr || lv_tabview_get_tab_act(tabview) != kBassTab) {
    return;
  }
  const bool live = Leds::bassLive();
  if (!meterLiveValid || live != meterLive) {
    meterLive = live;
    meterLiveValid = true;
    meterKicksValid = false;
    setMeterHint(live);
    lv_label_set_text(bassReading, live ? "" : "IDLE");
    lv_obj_invalidate(bassMeterObj);
  }
  if (!live) {
    return;
  }

  const BassDsp::Meter mt = Leds::bassMeter();
  if (!meterKicksValid) {
    meterKicks = mt.kicks;  // not a kick: just the counter's first reading
    meterKicksValid = true;
  } else if (mt.kicks != meterKicks) {
    meterKicks = mt.kicks;
    meterKickAt = lv_tick_get();
  }
  const bool kickLit = meterKickAt != 0 && lv_tick_elaps(meterKickAt) < 150;
  const uint8_t gate = Leds::bass(BASS_GATE);
  if (mt.level == meterLevel && mt.trigger == meterTrigger && mt.out == meterOut &&
      gate == meterGate && kickLit == meterKickLit) {
    return;
  }
  meterLevel = mt.level;
  meterTrigger = mt.trigger;
  meterOut = mt.out;
  meterGate = gate;
  meterKickLit = kickLit;

  char buf[24];
  snprintf(buf, sizeof(buf), "IN %u \xC2\xB7 OUT %u%%", (unsigned)mt.level,
           (unsigned)((mt.out * 100u + 127u) / 255u));
  lv_label_set_text(bassReading, buf);
  lv_obj_set_style_bg_color(bassKickPip, kickLit ? col(PHOSPHOR) : col(LINE_STRONG), 0);
  lv_obj_invalidate(bassMeterObj);
}

// A small legend swatch: the mark's colour in the mark's shape.
void legendSwatch(lv_obj_t *page, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                  uint32_t colour) {
  lv_obj_t *sw = slab(page, x, y, w, h);
  lv_obj_set_style_bg_color(sw, col(colour), 0);
  lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
}

// One option button in a row; the running choice is styled like the running
// mode, phosphor on phosphor-soft.
lv_obj_t *optionButton(lv_obj_t *page, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                       const char *text, lv_event_cb_t cb, uintptr_t index,
                       lv_obj_t **labelOut) {
  lv_obj_t *btn = lv_obj_create(page);
  lv_obj_remove_style_all(btn);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, 28);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(btn, &styleCard, 0);
  lv_obj_add_style(btn, &styleCardPressed, LV_STATE_PRESSED);
  lv_obj_add_style(btn, &styleCardChecked, LV_STATE_CHECKED);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)index);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &at_mono_11, 0);
  lv_obj_center(label);
  *labelOut = label;
  return btn;
}

void setOptionActive(lv_obj_t *btn, lv_obj_t *label, bool active) {
  if (active) {
    lv_obj_add_state(btn, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(btn, LV_STATE_CHECKED);
  }
  lv_obj_set_style_text_color(label, active ? col(PHOSPHOR) : col(INK_MUTED), 0);
}

void buildBassPage(lv_obj_t *page) {
  for (uint16_t &v : shownBass) {
    v = 0xFFFF;
  }
  constexpr lv_coord_t inset = PAD + 2;
  constexpr lv_coord_t blockW = SCREEN_W - 2 * inset;

  eyebrow(page, "INPUT", inset, 0);
  bassReading = lv_label_create(page);
  lv_label_set_text(bassReading, "");
  lv_obj_set_style_text_font(bassReading, &at_mono_13, 0);
  lv_obj_set_style_text_color(bassReading, col(INK), 0);
  lv_obj_align(bassReading, LV_ALIGN_TOP_RIGHT, -inset, -2);

  bassMeterObj = slab(page, inset, 16, blockW, 18);
  lv_obj_add_event_cb(bassMeterObj, onMeterDraw, LV_EVENT_DRAW_MAIN, nullptr);

  // Shown over the empty bar outside Bass mode, when the sampler is stopped.
  bassMeterHint = lv_label_create(bassMeterObj);
  lv_label_set_text(bassMeterHint, "SELECT BASS TO SEE THE INPUT");
  lv_obj_set_style_text_font(bassMeterHint, &at_mono_11, 0);
  lv_obj_set_style_text_color(bassMeterHint, col(INK_MUTED), 0);
  lv_obj_set_style_text_letter_space(bassMeterHint, 1, 0);
  lv_obj_center(bassMeterHint);

  // Legend: every mark is a shape and a word, never colour alone.
  constexpr lv_coord_t ly = 40;
  legendSwatch(page, inset, ly + 1, 2, 10, AMBER);
  eyebrow(page, "GATE", inset + 7, ly);
  legendSwatch(page, inset + 52, ly + 1, 3, 3, INK);
  legendSwatch(page, inset + 52, ly + 8, 3, 3, INK);
  eyebrow(page, "TRIGGER", inset + 62, ly);
  bassKickPip = slab(page, inset + blockW - 42, ly + 2, 8, 8);
  lv_obj_set_style_bg_color(bassKickPip, col(LINE_STRONG), 0);
  lv_obj_set_style_bg_opa(bassKickPip, LV_OPA_COVER, 0);
  eyebrow(page, "KICK", inset + blockW - 30, ly);

  static const char *const kTitles[4] = {"SENSITIVITY", "NOISE GATE", "KICK", "DECAY"};
  for (uint8_t i = 0; i < 4; i++) {
    bassSlider[i] = buildSliderBlock(page, inset, (lv_coord_t)(58 + i * 38), blockW,
                                     kTitles[i], 0, 255, onBassSlider, &bassValue[i]);
    lv_obj_set_user_data(bassSlider[i], (void *)(uintptr_t)i);
  }

  // Kick and Full are one choice; Hum is a separate filter switch, so it sits
  // apart from the pair, as Flash does below.
  constexpr lv_coord_t gap = 6;
  constexpr lv_coord_t toggleW = 82;
  const lv_coord_t pairW = (blockW - toggleW - 3 * gap) / 2;
  eyebrow(page, "BAND", inset, 214);
  static const char *const kBands[BassDsp::BAND_COUNT] = {"KICK", "FULL"};
  for (uint8_t i = 0; i < BassDsp::BAND_COUNT; i++) {
    bandBtn[i] = optionButton(page, (lv_coord_t)(inset + i * (pairW + gap)), 230, pairW,
                              kBands[i], onBandPressed, i, &bandLabel[i]);
  }
  humBtn = optionButton(page, (lv_coord_t)(inset + blockW - toggleW), 230, toggleW, "HUM",
                        onHumPressed, 0, &humLabel);

  eyebrow(page, "STYLE", inset, 266);
  static const char *const kStyles[BASS_STYLE_COUNT] = {"METER", "PULSE", "CENTRE"};
  const lv_coord_t styleW = (blockW - 2 * gap) / 3;
  for (uint8_t i = 0; i < BASS_STYLE_COUNT; i++) {
    styleBtn[i] = optionButton(page, (lv_coord_t)(inset + i * (styleW + gap)), 282, styleW,
                               kStyles[i], onStylePressed, i, &styleLabel[i]);
  }

  // Violet, Mine and Rainbow are one choice; Flash is a separate toggle, so it
  // sits apart from the three rather than reading as a fourth option -- the
  // gap before it is wider than the gaps within the group.
  eyebrow(page, "COLOUR", inset, 318);
  static const char *const kColours[BASS_COLOUR_COUNT] = {"VIOLET", "MINE", "RAINBOW"};
  const lv_coord_t colourW = (blockW - toggleW - 4 * gap) / 3;
  for (uint8_t i = 0; i < BASS_COLOUR_COUNT; i++) {
    colourBtn[i] = optionButton(page, (lv_coord_t)(inset + i * (colourW + gap)), 334, colourW,
                                kColours[i], onColourPressed, i, &colourLabel[i]);
  }
  flashBtn = optionButton(page, (lv_coord_t)(inset + blockW - toggleW), 334, toggleW, "FLASH",
                          onFlashPressed, 0, &flashLabel);

  makeScrollable(page, 8);
  lv_timer_create(meterTimerCb, 33, nullptr);
}

// One "Label ........ reading" row plus the hairline under it. Labels are
// prose (Grotesk), readings are instrument output (Mono).
lv_obj_t *buildInfoRow(lv_obj_t *page, const char *label, const char *value,
                       lv_coord_t y, bool separator) {
  lv_obj_t *l = lv_label_create(page);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, &at_sans_13, 0);
  lv_obj_set_style_text_color(l, col(INK_MUTED), 0);
  lv_obj_set_pos(l, PAD, y);

  lv_obj_t *v = lv_label_create(page);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_font(v, &at_mono_13, 0);
  lv_obj_set_style_text_color(v, col(INK), 0);
  lv_obj_align(v, LV_ALIGN_TOP_RIGHT, -PAD, y - 1);

  if (separator) {
    hairline(page, PAD, y + 19, SCREEN_W - 2 * PAD, 1);
  }
  return v;
}

void buildSystemPage(lv_obj_t *page) {
  // U+00B7 rather than LV_SYMBOL_BULLET: the brand fonts carry ASCII plus a
  // middle dot, em dash and degree, and none of LVGL's symbol glyphs.
  char hw[48];
  snprintf(hw, sizeof(hw), "GPIO%d \xC2\xB7 max %d px", PIN_LED_DATA, LED_COUNT_MAX);

  buildInfoRow(page, "Firmware", "v" HELMET_FW_VERSION, 6, true);
  buildInfoRow(page, "LED strip", hw, 29, true);
  sysBleValue = buildInfoRow(page, "Bluetooth", "advertising", 52, true);
  sysUptimeValue = buildInfoRow(page, "Uptime", "0m 00s", 75, false);

  lv_obj_t *btn = lv_obj_create(page);
  lv_obj_remove_style_all(btn);
  lv_obj_set_pos(btn, PAD, 94);
  lv_obj_set_size(btn, 150, 24);
  lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_style(btn, &styleButton, 0);
  lv_obj_add_style(btn, &styleButtonPressed, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn, onCalibratePressed, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, "Calibrate touch");
  lv_obj_center(l);

  // House sign-off, set like a terminal comment.
  lv_obj_t *sign = lv_label_create(page);
  lv_label_set_text(sign, "// alientrails");
  lv_obj_set_style_text_font(sign, &at_mono_11, 0);
  lv_obj_set_style_text_color(sign, col(INK_MUTED), 0);
  lv_obj_align(sign, LV_ALIGN_TOP_RIGHT, -PAD, 100);
}

void styleTabBar(lv_obj_t *tv) {
  lv_obj_t *btns = lv_tabview_get_tab_btns(tv);
  lv_obj_set_style_bg_color(btns, col(HULL), 0);
  lv_obj_set_style_bg_opa(btns, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(btns, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(btns, 1, 0);
  lv_obj_set_style_border_color(btns, col(LINE), 0);
  lv_obj_set_style_border_opa(btns, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(btns, 0, 0);
  lv_obj_set_style_text_font(btns, &at_mono_11, 0);
  lv_obj_set_style_text_letter_space(btns, 2, 0);

  lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_border_width(btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(btns, col(INK_MUTED), LV_PART_ITEMS);

  // Selected tab: ink label under a 2px ion bar — ion is the trail, the
  // "you are here" colour.
  lv_obj_set_style_bg_opa(btns, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(btns, col(INK), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_side(btns, LV_BORDER_SIDE_TOP,
                               LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(btns, 2, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(btns, col(ION), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_opa(btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);

  lv_obj_set_style_bg_color(btns, col(HULL_HIGH), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
}

void preparePage(lv_obj_t *page) {
  lv_obj_set_style_pad_all(page, 0, 0);
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

// ---------------------------------------------------------------------------
// Splash: the Alien Trails Labs logo
// ---------------------------------------------------------------------------

constexpr const char *kSplashLine = "HELMET LED // v" HELMET_FW_VERSION;
constexpr lv_coord_t kSplashLetterSpace = 2;
char splashTyped[40];
lv_obj_t *splashCursor = nullptr;
lv_coord_t splashLineX = 0;

void animOpaCb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void animZoomCb(void *obj, int32_t v) { lv_img_set_zoom((lv_obj_t *)obj, (uint16_t)v); }

// Types the status line out like a terminal, one character per step.
void animTypeCb(void *obj, int32_t v) {
  const size_t n = (size_t)v < sizeof(splashTyped) - 1 ? (size_t)v : sizeof(splashTyped) - 1;
  memcpy(splashTyped, kSplashLine, n);
  splashTyped[n] = '\0';
  lv_label_set_text_static((lv_obj_t *)obj, splashTyped);

  // The block cursor rides at the end of what has been typed so far.
  lv_point_t typed;
  lv_txt_get_size(&typed, splashTyped, &at_mono_11, kSplashLetterSpace, 0, LV_COORD_MAX,
                  LV_TEXT_FLAG_NONE);
  lv_obj_set_x(splashCursor, splashLineX + typed.x + 3);
}

void animBlinkCb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, v ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void splashDoneCb(lv_anim_t *a) { lv_obj_del((lv_obj_t *)a->var); }

void startAnim(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
               uint32_t delayMs, lv_anim_path_cb_t path) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, var);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_delay(&a, delayMs);
  lv_anim_set_path_cb(&a, path);
  lv_anim_start(&a);
}

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

void syncLink(bool connected) {
  // Connected is "online" (phosphor, solid disc); advertising is "scanning"
  // (ion, ring) — the design system's tag tones, so shape alone tells them
  // apart.
  const uint32_t fg = connected ? PHOSPHOR : ION;
  lv_obj_set_style_bg_color(linkTag, col(connected ? PHOSPHOR_SOFT : ION_SOFT), 0);
  lv_obj_set_style_text_color(linkLabel, col(fg), 0);
  lv_label_set_text(linkLabel, connected ? "LINK" : "BLE");
  lv_obj_set_style_bg_color(linkPip, col(fg), 0);
  lv_obj_set_style_bg_opa(linkPip, connected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(linkPip, col(fg), 0);
  lv_obj_set_style_border_opa(linkPip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(linkPip, connected ? 0 : 2, 0);
}

}  // namespace

void build() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, col(VOID), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildTopBar(scr);
  buildControlsRow(scr);

  tabview = lv_tabview_create(scr, LV_DIR_BOTTOM, TABBAR_H);
  lv_obj_set_pos(tabview, 0, TABVIEW_TOP);
  lv_obj_set_size(tabview, SCREEN_W, SCREEN_H - TABVIEW_TOP);
  lv_obj_set_style_bg_color(tabview, col(VOID), 0);
  lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
  styleTabBar(tabview);

  lv_obj_t *content = lv_tabview_get_content(tabview);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

  // Order is load-bearing: kBassTab is this page's index.
  lv_obj_t *modesPage = lv_tabview_add_tab(tabview, "MODES");
  lv_obj_t *colorPage = lv_tabview_add_tab(tabview, "COLOUR");
  lv_obj_t *bassPage = lv_tabview_add_tab(tabview, "BASS");
  lv_obj_t *stripPage = lv_tabview_add_tab(tabview, "STRIP");
  lv_obj_t *systemPage = lv_tabview_add_tab(tabview, "SYSTEM");
  preparePage(modesPage);
  preparePage(colorPage);
  preparePage(bassPage);
  preparePage(stripPage);
  preparePage(systemPage);

  buildModesPage(modesPage);
  buildColorPage(colorPage);
  buildBassPage(bassPage);
  buildStripPage(stripPage);
  buildSystemPage(systemPage);
}

void showSplash(uint32_t holdMs) {
  lv_obj_t *splash = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(splash);
  lv_obj_set_pos(splash, 0, 0);
  lv_obj_set_size(splash, SCREEN_W, SCREEN_H);
  lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(splash, LV_OBJ_FLAG_CLICKABLE);  // swallow stray touches
  lv_obj_set_style_bg_color(splash, col(VOID), 0);
  lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);

  // Mark over wordmark, separated by the mark's clear space (a quarter of its
  // height). Both are the untouched raster masters.
  constexpr lv_coord_t markH = 100;
  constexpr lv_coord_t wordH = 38;
  constexpr lv_coord_t gap = markH / 4;
  constexpr lv_coord_t top = (SCREEN_H - (markH + gap + wordH)) / 2 - 12;

  lv_obj_t *mark = lv_img_create(splash);
  lv_img_set_src(mark, &at_img_mark_splash);
  lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, top);

  lv_obj_t *word = lv_img_create(splash);
  lv_img_set_src(word, &at_img_wordmark_splash);
  lv_obj_align(word, LV_ALIGN_TOP_MID, 0, top + markH + gap);

  // The status line is left-anchored at its final centred position, so it
  // types out in place instead of re-centring on every character.
  lv_point_t full;
  lv_txt_get_size(&full, kSplashLine, &at_mono_11, kSplashLetterSpace, 0, LV_COORD_MAX,
                  LV_TEXT_FLAG_NONE);
  constexpr lv_coord_t cursorW = 6;
  const lv_coord_t lineX = (SCREEN_W - (full.x + 3 + cursorW)) / 2;
  splashLineX = lineX;
  const lv_coord_t lineY = SCREEN_H - 30;

  lv_obj_t *line = lv_label_create(splash);
  splashTyped[0] = '\0';
  lv_label_set_text_static(line, splashTyped);
  lv_obj_set_style_text_font(line, &at_mono_11, 0);
  lv_obj_set_style_text_color(line, col(INK_MUTED), 0);
  lv_obj_set_style_text_letter_space(line, kSplashLetterSpace, 0);
  lv_obj_set_pos(line, lineX, lineY);

  lv_obj_t *cursor = slab(splash, lineX, lineY + 1, cursorW, 12);
  splashCursor = cursor;
  lv_obj_set_style_bg_color(cursor, col(PHOSPHOR), 0);
  lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
  lv_obj_set_style_opa(cursor, LV_OPA_TRANSP, 0);

  // Timeline: the mark pops in, the wordmark follows, the status line types
  // and ends on a blinking block cursor, then the whole card fades to the UI.
  lv_obj_set_style_opa(mark, LV_OPA_TRANSP, 0);
  lv_obj_set_style_opa(word, LV_OPA_TRANSP, 0);
  lv_img_set_zoom(mark, 160);
  startAnim(mark, animOpaCb, LV_OPA_TRANSP, LV_OPA_COVER, 220, 80, lv_anim_path_ease_out);
  startAnim(mark, animZoomCb, 160, LV_IMG_ZOOM_NONE, 380, 80, lv_anim_path_overshoot);
  startAnim(word, animOpaCb, LV_OPA_TRANSP, LV_OPA_COVER, 300, 420, lv_anim_path_ease_out);

  const uint32_t typeStart = 760;
  const uint32_t typeMs = (uint32_t)strlen(kSplashLine) * 35;  // 35ms a character
  startAnim(line, animTypeCb, 0, (int32_t)strlen(kSplashLine), typeMs, typeStart,
            lv_anim_path_linear);

  lv_anim_t blink;
  lv_anim_init(&blink);
  lv_anim_set_var(&blink, cursor);
  lv_anim_set_exec_cb(&blink, animBlinkCb);
  lv_anim_set_values(&blink, 1, 0);
  lv_anim_set_time(&blink, 500);
  lv_anim_set_playback_time(&blink, 500);
  lv_anim_set_repeat_count(&blink, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&blink, lv_anim_path_step);
  lv_anim_set_delay(&blink, typeStart);
  lv_anim_set_early_apply(&blink, false);  // stay hidden until typing starts
  lv_anim_start(&blink);

  lv_anim_t out;
  lv_anim_init(&out);
  lv_anim_set_var(&out, splash);
  lv_anim_set_values(&out, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_exec_cb(&out, animOpaCb);
  lv_anim_set_ready_cb(&out, splashDoneCb);
  lv_anim_set_time(&out, 300);
  lv_anim_set_delay(&out, holdMs);
  lv_anim_start(&out);
}

void sync(uint32_t uptimeSec) {
  const uint8_t mode = Leds::mode();
  if (mode != shownMode) {
    for (int i = 0; i < MODE_COUNT; i++) {
      if (i == mode) {
        lv_obj_add_state(modeBtn[i], LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(modeBtn[i], LV_STATE_CHECKED);
      }
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
    lv_obj_set_style_text_color(modeTitle, col(on ? INK : INK_MUTED), 0);
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

  const uint8_t speed = Leds::speed();
  if (speed != shownSpeed) {
    if (!lv_obj_has_state(speedSlider, LV_STATE_PRESSED)) {
      lv_slider_set_value(speedSlider, speed, LV_ANIM_OFF);
    }
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", Leds::speedPercent());
    lv_label_set_text(speedValue, pct);
    shownSpeed = speed;
  }

  const uint8_t order = Leds::order();
  if (order != shownOrder) {
    for (int i = 0; i < ORDER_COUNT; i++) {
      const bool active = (i == order);
      if (active) {
        lv_obj_add_state(orderBtn[i], LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(orderBtn[i], LV_STATE_CHECKED);
      }
      lv_obj_set_style_text_color(orderBtnLabel[i],
                                  active ? col(PHOSPHOR) : col(INK_MUTED), 0);
    }
    shownOrder = order;
  }

  const uint16_t count = Leds::count();
  if (count != shownCount) {
    if (!lv_obj_has_state(countSlider, LV_STATE_PRESSED)) {
      lv_slider_set_value(countSlider, count, LV_ANIM_OFF);
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%u px", (unsigned)count);
    lv_label_set_text(countValue, buf);
    shownCount = count;
  }

  // Paired with lit() so the reading answers "and how many are left?" without
  // the arithmetic -- that is the number you are actually watching while you
  // slide this.
  const uint16_t skip = Leds::skip();
  const uint16_t litCount = Leds::lit();
  if (skip != shownSkip || litCount != shownLit) {
    if (!lv_obj_has_state(skipSlider, LV_STATE_PRESSED)) {
      lv_slider_set_value(skipSlider, skip, LV_ANIM_OFF);
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%u \xC2\xB7 %u lit", (unsigned)skip,
             (unsigned)litCount);
    lv_label_set_text(skipValue, buf);
    shownSkip = skip;
    shownLit = litCount;
  }

  const uint16_t maxMa = Leds::maxMilliamps();
  if (maxMa != shownMaxMa) {
    if (!lv_obj_has_state(maxMaSlider, LV_STATE_PRESSED)) {
      lv_slider_set_value(maxMaSlider, maxMa, LV_ANIM_OFF);
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%u mA", (unsigned)maxMa);
    lv_label_set_text(maxMaValue, buf);
    shownMaxMa = maxMa;
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

  // Bass settings. Readings are in the units each control means: sensitivity
  // and kick as a share of travel, the gate in the meter's own units so it
  // matches the bar, decay as the time constant the detector really uses.
  for (uint8_t i = 0; i < 4; i++) {
    const uint8_t v = Leds::bass(i);
    if (v == shownBass[i]) {
      continue;
    }
    if (!lv_obj_has_state(bassSlider[i], LV_STATE_PRESSED)) {
      lv_slider_set_value(bassSlider[i], v, LV_ANIM_OFF);
    }
    char buf[12];
    if (i == BASS_GATE) {
      snprintf(buf, sizeof(buf), "%u", (unsigned)v);
    } else if (i == BASS_DECAY) {
      snprintf(buf, sizeof(buf), "%u ms", (unsigned)(BassDsp::decayMs(v) + 0.5f));
    } else {
      snprintf(buf, sizeof(buf), "%u%%", (unsigned)((v * 100u + 127u) / 255u));
    }
    lv_label_set_text(bassValue[i], buf);
    shownBass[i] = v;
  }
  const uint8_t band = Leds::bass(BASS_BAND);
  if (band != shownBass[BASS_BAND]) {
    for (uint8_t i = 0; i < BassDsp::BAND_COUNT; i++) {
      setOptionActive(bandBtn[i], bandLabel[i], i == band);
    }
    shownBass[BASS_BAND] = band;
  }
  const uint8_t style = Leds::bass(BASS_STYLE);
  if (style != shownBass[BASS_STYLE]) {
    for (uint8_t i = 0; i < BASS_STYLE_COUNT; i++) {
      setOptionActive(styleBtn[i], styleLabel[i], i == style);
    }
    shownBass[BASS_STYLE] = style;
  }
  const uint8_t flags = Leds::bass(BASS_FLAGS);
  if (flags != shownBass[BASS_FLAGS]) {
    const uint8_t colour = bassColour(flags);
    for (uint8_t i = 0; i < BASS_COLOUR_COUNT; i++) {
      setOptionActive(colourBtn[i], colourLabel[i], i == colour);
    }
    setOptionActive(flashBtn, flashLabel, (flags & BASS_FLAG_FLASH) != 0);
    shownBass[BASS_FLAGS] = flags;
  }
  const uint8_t hum = Leds::bass(BASS_HUM);
  if (hum != shownBass[BASS_HUM]) {
    setOptionActive(humBtn, humLabel, hum != 0);
    shownBass[BASS_HUM] = hum;
  }

  lv_bar_set_value(pulseBar, on ? Leds::outputLevel() : 0, LV_ANIM_OFF);

  const bool ble = Ble::connected();
  if (!shownBleValid || ble != shownBle) {
    syncLink(ble);
    lv_label_set_text(sysBleValue, ble ? "connected" : "advertising");
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
