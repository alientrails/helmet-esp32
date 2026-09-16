#pragma once

// BLE control for the Android/phone app. GATT service with a write
// characteristic for commands and a notify characteristic for live state.
//
// Service:   7b1e8f00-2a5d-4e1a-9c3a-2f6b1d8a10e0
// Control (write, no response) 7b1e8f01-...: 2 bytes [opcode, value]
//   0x01 SET_MODE        value = mode index (see HelmetMode)
//   0x02 SET_BRIGHTNESS  value = 0..255
//   0x03 SET_ON          value = 0/1
//   0x04 TOGGLE          value ignored
//   0x05 NUDGE_BRIGHTNESS value = signed delta (int8_t)
//   0x06 SET_COLOR       value = 3 bytes [r, g, b]
// State (read/notify) 7b1e8f02-...: 7 bytes
//   [mode, brightness, on, outputLevel, r, g, b]

namespace Ble {
void begin();
void loop();
bool connected();  // true while a central is subscribed/linked
}  // namespace Ble
