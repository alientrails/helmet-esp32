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
//   0x0D SET_SCREEN      value = 0 screen off (pocket mode), 1 screen on.
//                        Off blanks the helmet's own display and ignores its
//                        touch or buttons; the lights, the mic and this link
//                        all carry on. The phone is the only way in or out of
//                        it -- see display.h for why that asymmetry matters.
// State (read/notify) 7b1e8f02-...: 20 bytes, the most one notification
// carries at the default MTU
//   [mode, brightness, on, outputLevel, r, g, b, speed, order, count,
//    maxMa/10, skip, bassSens, bassGate, bassKick, bassDecay, bassBand,
//    bassStyle, bassFlags, bassHum]
//   Settings notify as they change; outputLevel alone at most 10 Hz.
// Screen (read/notify) 7b1e8f06-...: 1 byte
//   [screenOn]  1 while the helmet's display and controls are live, 0 in
//   pocket mode. Sent on connect and whenever it changes.
//
//   This is a characteristic of its own rather than a 20th byte of the state,
//   because the state payload is already exactly the 20 bytes one
//   notification carries at the default MTU. It doubles as the capability
//   flag: firmware too old to blank its screen does not offer the
//   characteristic at all, so the app hides the control rather than sending
//   an opcode that would be ignored.
// Bass meter (read/notify) 7b1e8f03-...: 4 bytes
//   [level, trigger, out, kicks] -- level and trigger in the gate's units,
//   out 0..255, kicks a wrapping counter. ~25 Hz, only in Bass mode and only
//   while subscribed.
//
// Firmware update. Two characteristics of its own rather than more opcodes on
// the control one: an image is megabytes against the two bytes everything
// else sends, and it wants the largest MTU the phone will agree to.
// OTA control (write/notify) 7b1e8f04-...
//   host -> helmet:
//     0x01 BEGIN  9 bytes [0x01, size:4 LE, crc32:4 LE]
//     0x02 END    1 byte  -- all of it has been sent, check and commit
//     0x03 ABORT  1 byte  -- give up, leave the running slot alone
//   helmet -> host (notify), 6 bytes:
//     [state, error, received:4 LE]  state/error are Ota::State / Ota::Error
//   Sent on BEGIN, every ~250 ms while receiving, and on END or ABORT. The
//   progress notifications double as flow control: the app keeps its writes
//   within a window of what the helmet has acknowledged receiving.
// OTA data (write without response) 7b1e8f05-...: raw image bytes, in order,
//   up to MTU-3 at a time. Only meaningful between BEGIN and END.
//
// A transfer writes into the slot the helmet is not running from, and nothing
// is switched over until END has checked the CRC, so an interrupted update
// leaves the helmet exactly as it was.
//
// The state payload only ever grows at the end, so a client that reads the
// first four bytes and stops keeps working across firmware updates.

namespace Ble {
void begin();
void loop();
bool connected();  // true while a central is subscribed/linked
}  // namespace Ble
