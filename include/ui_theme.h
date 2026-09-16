#pragma once

// Visual language for the CYD touchscreen UI: one dark palette, a handful of
// shared LVGL styles, and small builders so every screen is put together from
// the same parts. Keep all colour/spacing decisions here — screens in
// ui_touch.cpp should only compose.

#include <lvgl.h>

namespace UiTheme {

// Palette. Cool near-black base, two elevation steps, one blue accent. The
// LED's own colour is the only saturated thing on screen, so the chrome stays
// deliberately desaturated and never fights it.
constexpr uint32_t BG = 0x0B0E14;        // page
constexpr uint32_t SURFACE = 0x141924;   // top bar, tab bar
constexpr uint32_t CARD = 0x1C2230;      // resting control
constexpr uint32_t CARD_LOW = 0x161B27;  // gradient foot of a card
constexpr uint32_t CARD_HI = 0x252D3D;   // pressed / hover
constexpr uint32_t LINE = 0x2B3446;      // hairlines and borders
constexpr uint32_t TEXT = 0xE7ECF5;      // primary type
constexpr uint32_t TEXT_DIM = 0xA4B0C4;  // secondary type
constexpr uint32_t MUTED = 0x6D7A90;     // labels, inactive icons
constexpr uint32_t ACCENT = 0x2F86FF;    // selection, slider fill
constexpr uint32_t ACCENT_DK = 0x1C63D8;
constexpr uint32_t ACCENT_LT = 0x74B4FF;
constexpr uint32_t LIVE = 0x34D399;   // power switch "on"

// Geometry shared across screens.
constexpr lv_coord_t SCREEN_W = 320;
constexpr lv_coord_t SCREEN_H = 240;
constexpr lv_coord_t TOPBAR_H = 36;
constexpr lv_coord_t PULSE_H = 3;  // live output strip under the top bar
constexpr lv_coord_t BRIGHT_TOP = TOPBAR_H + PULSE_H;
constexpr lv_coord_t BRIGHT_H = 41;
constexpr lv_coord_t TABVIEW_TOP = BRIGHT_TOP + BRIGHT_H;
constexpr lv_coord_t TABBAR_H = 36;
constexpr lv_coord_t PAGE_H = SCREEN_H - TABVIEW_TOP - TABBAR_H;
constexpr lv_coord_t PAD = 10;

inline lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// Builds the shared styles. Call once, after lv_init().
void init();

// Shared styles, valid after init().
extern lv_style_t styleCard;         // resting panel / mode button
extern lv_style_t styleCardPressed;  // any pressable card, held
extern lv_style_t styleCardChecked;  // selected mode: accent fill + glow
extern lv_style_t styleFlat;         // zero-chrome container

// A borderless, non-scrolling, padding-free container — the building block for
// every layout slab. LVGL's default theme adds chrome we almost never want.
lv_obj_t *slab(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);

// Small all-caps label used for section headers ("BRIGHTNESS", "PRESETS").
lv_obj_t *eyebrow(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y);

// Picks black or white text so it stays legible on an arbitrary swatch.
lv_color_t contrastOn(uint8_t r, uint8_t g, uint8_t b);

}  // namespace UiTheme
