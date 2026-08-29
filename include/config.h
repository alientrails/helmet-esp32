#pragma once

// Helmet LED controller — Seeed XIAO ESP32, USB power bank, 0.91" OLED.

#define HELMET_FW_VERSION "1.3.0"

// 0.91" modules are almost always SSD1306 128x32 at 0x3C.
// If the screen is blank, try 0x3D. If you actually have a 0.96" 128x64,
// set OLED_HEIGHT to 64.
#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_ADDRESS 0x3C
#define OLED_ROTATION 0  // 0 = USB-C up relative to the silk, 2 = flipped

#if defined(HELMET_BOARD_XIAO_S3)
  // Seeed XIAO ESP32-S3
  #define PIN_BTN_LEFT 2    // D1
  #define PIN_BTN_RIGHT 3   // D2
  #define PIN_BTN_SELECT 4  // D3
  #define PIN_SDA 5         // D4
  #define PIN_SCL 6         // D5
  #define PIN_LED_DATA 9    // D10  WS2812B DIN
  #define PIN_MIC 1         // D0 analog
#else
  // Seeed XIAO ESP32-C3
  #define PIN_BTN_LEFT 3    // D1
  #define PIN_BTN_RIGHT 4   // D2
  #define PIN_BTN_SELECT 5  // D3
  #define PIN_SDA 6         // D4
  #define PIN_SCL 7         // D5
  #define PIN_LED_DATA 10   // D10  WS2812B DIN
  #define PIN_MIC 2         // D0 analog (GPIO2)
#endif

// MAX9814 / MAX4466 on D0. Burst-sample, keep ~60–120 Hz kicks, drop hats.
#define MIC_BURST 28
#define MIC_DC_ALPHA 0.003f
#define MIC_BASS_ALPHA 0.05f     // ~100 Hz low-pass
#define MIC_SUB_ALPHA 0.015f     // ~30 Hz, subtracted to make a kick band
#define MIC_ENV_ATTACK 0.40f
#define MIC_ENV_RELEASE 0.055f
#define MIC_FLOOR_ALPHA 0.004f
#define MIC_PUNCH_DECAY 0.91f
#define MIC_PUNCH_RATIO 1.48f
#define MIC_GATE 10.0f

#define BUTTON_INVERT 1  // INPUT_PULLUP, pressed = LOW

// Cut the BTF WS2812B reel to helmet length. Count pixels and set this.
// 60/m × 1 m ≈ 60 LEDs. Do not run the full 5 m / 300 px off USB.
#define LED_COUNT 60
#define LED_MAX_MA 1000
#define BRIGHTNESS_DEFAULT 96
#define BRIGHTNESS_STEP 8
#define BRIGHTNESS_MIN 8

#define DEBOUNCE_MS 25
#define LONG_PRESS_MS 650
#define REPEAT_FIRST_MS 380
#define REPEAT_MS 90

#define MENU_IDLE_MS 10000
#define SPLASH_MS 1200

#define PREFS_NAMESPACE "helmet"
