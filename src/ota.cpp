#include "ota.h"

#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace Ota {
namespace {

State gState = OTA_IDLE;
Error gError = OTA_OK;
uint32_t gTotal = 0;
uint32_t gGot = 0;
uint32_t gWantCrc = 0;
uint32_t gCrc = 0;

// CRC-32 (the zlib/PNG polynomial, reflected) computed as the image streams
// past, so a corrupt transfer is caught before anything is made bootable.
// Table-free: a byte-at-a-time loop costs microseconds against a flash write
// and saves the 1 KB a table would pin in RAM for the whole run.
uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
  }
  return ~crc;
}

void fail(Error e) {
  gError = e;
  gState = OTA_FAILED;
  if (Update.isRunning()) {
    Update.abort();
  }
}

}  // namespace

bool begin(uint32_t size, uint32_t crc) {
  if (gState == OTA_RECEIVING || gState == OTA_VERIFYING) {
    gError = OTA_ERR_BUSY;  // deliberately not fail(): the live one survives
    return false;
  }
  gState = OTA_IDLE;
  gError = OTA_OK;
  gGot = 0;
  gCrc = 0;
  gTotal = size;
  gWantCrc = crc;

  // A layout with one app slot cannot take an update at all. Saying so beats
  // letting Update.begin() fail with something less specific.
  if (esp_ota_get_next_update_partition(nullptr) == nullptr) {
    fail(OTA_ERR_NO_SLOT);
    return false;
  }
  if (size == 0 || size > kMaxImageBytes) {
    fail(OTA_ERR_TOO_BIG);
    return false;
  }
  if (!Update.begin(size, U_FLASH)) {
    fail(size > Update.size() ? OTA_ERR_TOO_BIG : OTA_ERR_WRITE);
    return false;
  }
  gState = OTA_RECEIVING;
  return true;
}

bool write(const uint8_t *data, size_t len) {
  if (gState != OTA_RECEIVING) {
    if (gState != OTA_FAILED) {
      fail(OTA_ERR_NOT_STARTED);
    }
    return false;
  }
  if (len == 0) {
    return true;
  }
  if (gGot + (uint32_t)len > gTotal) {
    fail(OTA_ERR_OVERRUN);
    return false;
  }
  if (Update.write((uint8_t *)data, len) != len) {
    fail(OTA_ERR_WRITE);
    return false;
  }
  gCrc = crc32Update(gCrc, data, len);
  gGot += (uint32_t)len;
  return true;
}

bool end() {
  // A second END for a transfer that already committed is the app repeating
  // itself, not an error: say yes again rather than turning an update that
  // worked into a reported failure.
  if (gState == OTA_DONE) {
    return true;
  }
  if (gState != OTA_RECEIVING) {
    if (gState != OTA_FAILED) {
      fail(OTA_ERR_NOT_STARTED);
    }
    return false;
  }
  gState = OTA_VERIFYING;
  if (gGot != gTotal) {
    fail(OTA_ERR_SHORT);
    return false;
  }
  // Our own check first. Update.end() validates the image's structure, but a
  // flipped bit inside the payload is ours to catch, and catching it here
  // means the slot is never marked bootable.
  if (gCrc != gWantCrc) {
    fail(OTA_ERR_CRC);
    return false;
  }
  if (!Update.end(true)) {
    fail(OTA_ERR_VERIFY);
    return false;
  }
  gState = OTA_DONE;
  gError = OTA_OK;
  return true;
}

void abort() {
  if (Update.isRunning()) {
    Update.abort();
  }
  gState = OTA_IDLE;
  gError = OTA_OK;
  gGot = 0;
  gTotal = 0;
  gCrc = 0;
}

void reboot() {
  Serial.flush();
  delay(100);  // let the last BLE notification and the log line get out
  ESP.restart();
}

State state() { return gState; }
Error error() { return gError; }
uint32_t received() { return gGot; }
uint32_t total() { return gTotal; }
bool active() { return gState == OTA_RECEIVING || gState == OTA_VERIFYING; }

}  // namespace Ota
