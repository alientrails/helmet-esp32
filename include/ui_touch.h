#pragma once

// Touchscreen UI for the CYD (ESP32-2432S028R) build. Replaces ui.h/ui.cpp,
// which drive the OLED + physical buttons on the XIAO builds.

namespace UiTouch {
void begin();
void loop();
void calibrate();       // blocking; run on demand once touch is confirmed working
void printTouchDebug();  // blocking ~5s; dumps raw touch reads to Serial
}  // namespace UiTouch
