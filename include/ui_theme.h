#pragma once

// Visual language for the CYD touchscreen UI, taken from the Alien Trails Labs
// design system (Void theme): a near-black ground, a phosphor-green signal and
// a blue ion trail. Every colour, radius and spacing decision lives here —
// screens in ui_screens.cpp only compose.
//
// The three house rules, as they apply to a helmet controller:
//   1. Dark first — everything sits on VOID, panels are raised with HULL and
//      separated with 1px LINE, not shadows.
//   2. Signal is scarce — PHOSPHOR marks what is live (lights on, the running
//      mode, a BLE link). Nothing decorative is ever phosphor.
//   3. Leave a trail — the dotted phosphor-to-ion trail appears once per view;
//      here it is the brightness slider.

#include <lvgl.h>

#include "brand.h"

namespace UiTheme {

// Colour tokens (Void theme).
constexpr uint32_t VOID = 0x050A12;         // page ground
constexpr uint32_t HULL = 0x0C1520;         // raised panels, bars
constexpr uint32_t HULL_HIGH = 0x142131;    // pressed / hovered
constexpr uint32_t LINE = 0x1F2D3F;         // decorative hairlines
constexpr uint32_t LINE_STRONG = 0x4D6480;  // control borders (3:1)
constexpr uint32_t INK = 0xE4F0EA;          // primary text
constexpr uint32_t INK_MUTED = 0x8FA3B6;    // labels, metadata
constexpr uint32_t PHOSPHOR = 0x3DFF8F;     // the signal: live / on
constexpr uint32_t PHOSPHOR_SOFT = 0x0B2A1B;
constexpr uint32_t ON_PHOSPHOR = 0x050A12;
constexpr uint32_t ION = 0x4AA8FF;  // the trail: links, selection, scanning
constexpr uint32_t ION_SOFT = 0x0B2140;
constexpr uint32_t AMBER = 0xFFB547;
constexpr uint32_t ALERT = 0xFF6B78;

// Spacing (4px base) and machined, small radii.
constexpr lv_coord_t SPACE_1 = 4;
constexpr lv_coord_t SPACE_2 = 8;
constexpr lv_coord_t SPACE_3 = 12;
constexpr lv_coord_t SPACE_4 = 16;
constexpr lv_coord_t RADIUS_SM = 2;
constexpr lv_coord_t RADIUS_MD = 6;

// Geometry, 320x240 landscape.
constexpr lv_coord_t SCREEN_W = 320;
constexpr lv_coord_t SCREEN_H = 240;
constexpr lv_coord_t PAD = SPACE_3;  // screen gutter
constexpr lv_coord_t TOPBAR_H = 38;
constexpr lv_coord_t PULSE_H = 2;  // live output meter under the top bar
constexpr lv_coord_t BRIGHT_TOP = TOPBAR_H + PULSE_H;
constexpr lv_coord_t BRIGHT_H = 44;
constexpr lv_coord_t TABVIEW_TOP = BRIGHT_TOP + BRIGHT_H;
constexpr lv_coord_t TABBAR_H = 34;
constexpr lv_coord_t PAGE_H = SCREEN_H - TABVIEW_TOP - TABBAR_H;

inline lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// Builds the shared styles. Call once, after lv_init().
void init();

// Shared styles, valid after init().
extern lv_style_t styleCard;          // resting mode button / panel
extern lv_style_t styleCardPressed;   // any pressable card, held
extern lv_style_t styleCardChecked;   // the running mode: phosphor on phosphor-soft
extern lv_style_t styleButton;        // secondary button: line-strong outline
extern lv_style_t styleButtonPressed;
extern lv_style_t styleFlat;          // zero-chrome container

// A borderless, non-scrolling, padding-free container — the building block for
// every layout slab. LVGL's default theme adds chrome we almost never want.
lv_obj_t *slab(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);

// A 1px hairline in LINE.
lv_obj_t *hairline(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);

// UPPERCASE Space Mono label ("BRIGHTNESS", "MODES") in INK_MUTED.
lv_obj_t *eyebrow(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);

// Picks void or white text so it stays legible on an arbitrary swatch.
lv_color_t contrastOn(uint8_t r, uint8_t g, uint8_t b);

}  // namespace UiTheme
