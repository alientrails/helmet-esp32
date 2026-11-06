#pragma once

// Pocket mode: the helmet's own screen and its controls, switched off as one
// piece.
//
// With the display off the CYD's panel is painted void, its backlight is
// dark and its touchscreen is ignored; on the XIAO builds the OLED is powered
// down and the three buttons do nothing. Everything else carries on exactly
// as before -- the strip keeps running its pattern, the bass detector keeps
// listening, BLE stays up and a firmware update still works. This is about
// the controls on the helmet, not about what the helmet is doing.
//
// It exists because the helmet's controller rides in a pocket. A touchscreen
// in there is a row of buttons being pressed by a thigh.
//
// Only the phone can switch it off, and only the phone can switch it back on
// (BLE opcode 0x0D, reported on the display characteristic -- see ble.h).
// That asymmetry is the point: a screen that could be woken by being sat on
// would not be off in any useful sense. The one escape hatch, if the phone is
// ever gone, is 'd' on the serial console.
//
// The state is saved like every other setting, so a power bank that blinks
// does not wake the screen up inside the pocket.

namespace Display {

// Pocket mode off (true) or on (false). Cheap; call it from a render loop.
bool on();

// Switches the screen and its controls off or on. Safe from any task: the UI
// layers notice on their next pass rather than being called back.
void setOn(bool on);

}  // namespace Display
