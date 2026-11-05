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
//   0x07 SET_SPEED       value = 0..255, 128 = 1x (SPEED_NORMAL)
//   0x08 SET_ORDER       value = wire colour order (see LedOrder)
//   0x09 SET_COUNT       value = pixels, 1..LED_COUNT_MAX
//   0x0A SET_MAX_MA      value = current budget in tens of mA (50 = 500 mA)
//   0x0B SET_SKIP        value = hidden pixels at the head, 0..LED_SKIP_MAX
//   0x0C SET_BASS        3 bytes [0x0C, param, value], param = BassParam:
//                        0 sensitivity, 1 gate, 2 kick, 3 decay, 4 band,
//                        5 style, 6 flags, 7 hum (see leds.h)
//                        flags bit 0 flash, bit 1 my colour, bit 2 rainbow.
//                        The two colour bits are one choice of three, so a
//                        client that sets both gets the order bassColour()
//                        defines: rainbow, then my colour, then violet.
//                        Firmware before 1.8.0 masks bit 2 off and answers
//                        with the flags it kept, so asking an older helmet for
//                        a rainbow leaves it on violet and says so.
// State (read/notify) 7b1e8f02-...: 20 bytes, the most one notification
// carries at the default MTU
//   [mode, brightness, on, outputLevel, r, g, b, speed, order, count,
//    maxMa/10, skip, bassSens, bassGate, bassKick, bassDecay, bassBand,
//    bassStyle, bassFlags, bassHum]
//   Settings notify as they change; outputLevel alone at most 10 Hz.
// Bass meter (read/notify) 7b1e8f03-...: 4 bytes
//   [level, trigger, out, kicks] -- level and trigger in the gate's units,
//   out 0..255, kicks a wrapping counter. ~25 Hz, only in Bass mode and only
//   while subscribed.
//
// The state payload only ever grows at the end, so a client that reads the
// first four bytes and stops keeps working across firmware updates.

namespace Ble {
void begin();
void loop();
bool connected();  // true while a central is subscribed/linked
}  // namespace Ble
