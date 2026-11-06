#include "display.h"

// Just the flag. The two UI layers (src/ui_touch.cpp for the CYD's panel,
// src/ui.cpp for the XIAO's OLED) poll it and blank themselves, which is the
// same way they pick up every other change made over BLE -- and it keeps the
// hardware out of whatever task set it.

namespace Display {
namespace {
bool gOn = true;
}  // namespace

bool on() { return gOn; }

void setOn(bool on) { gOn = on; }

}  // namespace Display
