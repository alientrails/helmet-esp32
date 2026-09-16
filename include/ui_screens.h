#pragma once

// The CYD touchscreen's screens, as pure LVGL. Deliberately free of Arduino,
// TFT_eSPI and touch-panel code so that the same source can be compiled for the
// host preview in tools/sim — the layout you see there is the layout that ships.
// ui_touch.cpp owns the hardware and drives this.

#include <lvgl.h>

namespace UiScreens {

// Builds the whole interface on the active screen. Call after lv_init(),
// after a display is registered, and after UiTheme::init().
void build();

// Full-screen overlay that holds for holdMs, fades out and deletes itself.
void showSplash(uint32_t holdMs);

// Pulls live state out of Leds::/Ble:: and into the widgets. Cheap to call
// often: it only touches LVGL for values that actually changed.
void sync(uint32_t uptimeSec);

// True exactly once after the System tab's calibrate button is pressed.
// Calibration blocks and paints directly, so it must not run inside
// lv_timer_handler() — the caller picks it up from its own loop instead.
bool takeCalibrationRequest();

// Switches tabs programmatically (used by the host preview to capture pages).
void showTab(uint8_t index);

}  // namespace UiScreens
