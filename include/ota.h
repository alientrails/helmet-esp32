#pragma once

#include <Arduino.h>

// Over-the-air firmware update, received over BLE from the phone app.
//
// The helmet has two app slots (min_spiffs.csv): it runs from one and writes
// the incoming image into the other, and only switches over once the whole
// thing has arrived and its CRC matches. A transfer that stops halfway --
// phone walks away, battery dies, app is killed -- leaves the running slot
// untouched and the half-written one simply never marked bootable, so the
// worst case of a failed update is that nothing happened.
//
// What this does NOT protect against is an image that is intact, boots, and
// is broken. Arduino's stock bootloader does not enable the IDF's rollback,
// so recovering from that still means a USB cable. Verify a build on the
// bench before sending it to a helmet you are about to wear.

namespace Ota {

enum State : uint8_t {
  OTA_IDLE = 0,     // nothing in progress
  OTA_RECEIVING,    // an image is arriving
  OTA_VERIFYING,    // all of it arrived, checking it
  OTA_DONE,         // written and marked bootable; reboot pending
  OTA_FAILED,       // gave up, see error()
};

// Why an update stopped. Sent to the app, which has the matching list.
enum Error : uint8_t {
  OTA_OK = 0,
  OTA_ERR_BUSY,        // one is already running
  OTA_ERR_NO_SLOT,     // no second app partition: the layout predates OTA
  OTA_ERR_TOO_BIG,     // the image does not fit the slot
  OTA_ERR_WRITE,       // flash write failed
  OTA_ERR_OVERRUN,     // more bytes arrived than were promised
  OTA_ERR_SHORT,       // END arrived before all of them did
  OTA_ERR_CRC,         // the image arrived corrupt
  OTA_ERR_VERIFY,      // the image is not a valid ESP32 app
  OTA_ERR_NOT_STARTED, // data or END with no BEGIN
};

// Largest image we will accept, so a bad size cannot start a doomed transfer.
constexpr uint32_t kMaxImageBytes = 1900u * 1024u;

// Starts a transfer of [size] bytes whose CRC-32 is [crc]. False if it could
// not start, and error() says why.
bool begin(uint32_t size, uint32_t crc);

// Appends the next [len] bytes. False on the first failure; the transfer is
// finished at that point and error() says why.
bool write(const uint8_t *data, size_t len);

// Checks the CRC and the image, and marks the new slot bootable. False and
// error() set if anything did not line up. Nothing is switched over until
// this returns true.
bool end();

// Throws away a transfer in progress and leaves the running slot alone. Safe
// to call at any time, including when nothing is running.
void abort();

// Reboots into the freshly written slot. Only meaningful after end() returned
// true; the caller decides when, so a notification can go out first.
void reboot();

State state();
Error error();
uint32_t received();
uint32_t total();

// True while a transfer is live, so the rest of the firmware can stand back:
// the UI shows progress instead of controls, and the strip stops animating.
bool active();

}  // namespace Ota
