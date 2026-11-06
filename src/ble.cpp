#include "ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "display.h"
#include "leds.h"
#include "ota.h"

namespace Ble {
namespace {

constexpr char kServiceUuid[] = "7b1e8f00-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kControlUuid[] = "7b1e8f01-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kStateUuid[] = "7b1e8f02-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kMeterUuid[] = "7b1e8f03-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kOtaCtrlUuid[] = "7b1e8f04-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kOtaDataUuid[] = "7b1e8f05-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kScreenUuid[] = "7b1e8f06-2a5d-4e1a-9c3a-2f6b1d8a10e0";

// The state is a set of settings plus the live output level. Settings go out
// the moment they change. The level on its own changes every frame in any
// animated mode, and sending it every frame used to flood the link with
// hundreds of notifications a second, so level-only updates are capped.
constexpr uint32_t kLevelMinMs = 100;  // 10 Hz
// The bass meter is its own characteristic, streamed only while someone is
// subscribed and the detector is running. 25 Hz reads as live on a meter.
constexpr uint32_t kMeterMinMs = 40;
// How often progress goes back during an update. The app paces itself off
// these, so too slow stalls the transfer and too fast spends the airtime the
// image needs. 250 ms is a readable progress bar and a wide enough window.
constexpr uint32_t kOtaProgressMs = 250;

enum Opcode : uint8_t {
  OP_SET_MODE = 0x01,
  OP_SET_BRIGHTNESS = 0x02,
  OP_SET_ON = 0x03,
  OP_TOGGLE = 0x04,
  OP_NUDGE_BRIGHTNESS = 0x05,
  OP_SET_COLOR = 0x06,
  OP_SET_SPEED = 0x07,
  OP_SET_ORDER = 0x08,
  OP_SET_COUNT = 0x09,
  OP_SET_MAX_MA = 0x0A,
  OP_SET_SKIP = 0x0B,
  OP_SET_BASS = 0x0C,
  OP_SET_SCREEN = 0x0D,
};

NimBLEServer *gServer = nullptr;
NimBLECharacteristic *gStateChar = nullptr;
NimBLECharacteristic *gMeterChar = nullptr;
NimBLECharacteristic *gOtaCtrlChar = nullptr;
NimBLECharacteristic *gScreenChar = nullptr;
uint32_t gLastOtaAt = 0;
bool gOtaRebootPending = false;
uint32_t gOtaRebootAt = 0;
uint32_t gLastPushAt = 0;
uint32_t gLastMeterAt = 0;
uint8_t gLastBass[BASS_PARAM_COUNT] = {};
bool gLastScreen = true;
bool gLastScreenValid = false;
bool gDirty = false;
uint8_t gLastMode = 0xFF;
uint8_t gLastBri = 0xFF;
bool gLastOn = true;
uint8_t gLastLevel = 0xFF;
uint8_t gLastR = 0xFF;
uint8_t gLastG = 0xFF;
uint8_t gLastB = 0xFF;
uint8_t gLastSpeed = 0xFF;
uint8_t gLastOrder = 0xFF;
uint16_t gLastCount = 0xFFFF;
uint16_t gLastMaxMa = 0xFFFF;
uint16_t gLastSkip = 0xFFFF;

void applyControl(const uint8_t *data, size_t len) {
  if (len < 1) {
    return;
  }
  const uint8_t opcode = data[0];
  const uint8_t value = len > 1 ? data[1] : 0;

  switch (opcode) {
    case OP_SET_MODE:
      Leds::setMode(value);
      break;
    case OP_SET_BRIGHTNESS:
      Leds::setBrightness(value);
      break;
    case OP_SET_ON:
      Leds::setOn(value != 0);
      break;
    case OP_TOGGLE:
      Leds::toggle();
      break;
    case OP_NUDGE_BRIGHTNESS:
      Leds::nudgeBrightness((int8_t)value);
      break;
    case OP_SET_COLOR:
      if (len < 4) {
        return;
      }
      Leds::setColor(data[1], data[2], data[3]);
      break;
    case OP_SET_SPEED:
      Leds::setSpeed(value);
      break;
    case OP_SET_ORDER:
      Leds::setOrder(value);
      break;
    case OP_SET_COUNT:
      Leds::setCount(value);
      break;
    case OP_SET_MAX_MA:
      // Sent in tens of mA so the whole budget still fits in the one-byte
      // value slot every other opcode uses.
      Leds::setMaxMilliamps((uint16_t)value * 10u);
      break;
    case OP_SET_SKIP:
      Leds::setSkip(value);
      break;
    case OP_SET_BASS:
      // Two value bytes: which setting (BassParam), then its value.
      if (len < 3) {
        return;
      }
      Leds::setBass(data[1], data[2]);
      break;
    case OP_SET_SCREEN:
      // Pocket mode. The UI layer blanks itself on its next pass; nothing
      // about the lights or this link changes.
      Display::setOn(value != 0);
      break;
    default:
      return;
  }
  gDirty = true;
}

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr) override {
    const std::string v = chr->getValue();
    applyControl(reinterpret_cast<const uint8_t *>(v.data()), v.size());
  }
};

// Progress and outcome for the app. Sent on every state change and every
// kOtaProgressMs while an image is arriving.
void pushOta() {
  if (gOtaCtrlChar == nullptr) {
    return;
  }
  const uint32_t got = Ota::received();
  const uint8_t payload[6] = {(uint8_t)Ota::state(),
                              (uint8_t)Ota::error(),
                              (uint8_t)(got & 0xFFu),
                              (uint8_t)((got >> 8) & 0xFFu),
                              (uint8_t)((got >> 16) & 0xFFu),
                              (uint8_t)((got >> 24) & 0xFFu)};
  gOtaCtrlChar->setValue(payload, sizeof(payload));
  gOtaCtrlChar->notify();
  gLastOtaAt = millis();
}

uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

class OtaCtrlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr) override {
    const std::string v = chr->getValue();
    if (v.empty()) {
      return;
    }
    const uint8_t *d = reinterpret_cast<const uint8_t *>(v.data());
    switch (d[0]) {
      case 0x01:  // BEGIN
        if (v.size() < 9) {
          return;
        }
        // The lights stop before the first byte lands: an update competes for
        // flash and CPU with the render loop, and a helmet flashing away while
        // its firmware is replaced invites someone to unplug it mid-write.
        Leds::setOn(false);
        Ota::begin(le32(d + 1), le32(d + 5));
        break;
      case 0x02:  // END
        if (Ota::end()) {
          pushOta();
          // Let the notification reach the phone before the radio goes away.
          gOtaRebootPending = true;
          gOtaRebootAt = millis();
          return;
        }
        break;
      case 0x03:  // ABORT
        Ota::abort();
        Leds::setOn(true);
        break;
      default:
        return;
    }
    pushOta();
  }
};

class OtaDataCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr) override {
    const std::string v = chr->getValue();
    if (v.empty()) {
      return;
    }
    if (!Ota::write(reinterpret_cast<const uint8_t *>(v.data()), v.size())) {
      pushOta();  // tell the app at once rather than at the next tick
    }
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer *server) override {
    // A transfer cannot continue without the phone, and a half-written slot
    // is only dangerous if something later decides to boot it. Dropping it
    // here means a walk out of range costs nothing but the upload.
    if (Ota::active()) {
      Ota::abort();
      Leds::setOn(true);
    }
    NimBLEDevice::startAdvertising();
  }
};

ControlCallbacks gControlCallbacks;
ServerCallbacks gServerCallbacks;
OtaCtrlCallbacks gOtaCtrlCallbacks;
OtaDataCallbacks gOtaDataCallbacks;

void pushState(bool force) {
  const uint8_t mode = Leds::mode();
  const uint8_t bri = Leds::brightness();
  const bool on = Leds::on();
  const uint8_t level = Leds::outputLevel();
  uint8_t r, g, b;
  Leds::color(&r, &g, &b);
  const uint8_t speed = Leds::speed();
  const uint8_t order = Leds::order();
  const uint16_t count = Leds::count();
  const uint16_t maxMa = Leds::maxMilliamps();
  const uint16_t skip = Leds::skip();
  uint8_t bass[BASS_PARAM_COUNT];
  bool bassSame = true;
  for (uint8_t i = 0; i < BASS_PARAM_COUNT; i++) {
    bass[i] = Leds::bass(i);
    bassSame = bassSame && bass[i] == gLastBass[i];
  }

  const bool settingsSame = mode == gLastMode && bri == gLastBri && on == gLastOn &&
                            r == gLastR && g == gLastG && b == gLastB &&
                            speed == gLastSpeed && order == gLastOrder &&
                            count == gLastCount && maxMa == gLastMaxMa &&
                            skip == gLastSkip && bassSame;
  if (!force && settingsSame) {
    if (level == gLastLevel) {
      return;
    }
    if (millis() - gLastPushAt < kLevelMinMs) {
      return;  // only the level moved, and it went out recently
    }
  }
  gLastMode = mode;
  gLastBri = bri;
  gLastOn = on;
  gLastLevel = level;
  gLastR = r;
  gLastG = g;
  gLastB = b;
  gLastSpeed = speed;
  gLastOrder = order;
  gLastCount = count;
  gLastMaxMa = maxMa;
  gLastSkip = skip;
  for (uint8_t i = 0; i < BASS_PARAM_COUNT; i++) {
    gLastBass[i] = bass[i];
  }

  // Appended at the end, which the state contract in ble.h guarantees is safe
  // for a client that stops reading early. 19 bytes: it has to stay within the
  // 20 a notification carries before the central negotiates a larger MTU --
  // and it is now exactly 20, so the next field needs a longer MTU or its own
  // characteristic.
  const uint8_t payload[20] = {mode,
                               bri,
                               (uint8_t)(on ? 1 : 0),
                               level,
                               r,
                               g,
                               b,
                               speed,
                               order,
                               (uint8_t)count,
                               (uint8_t)(maxMa / 10u),
                               (uint8_t)skip,
                               bass[BASS_SENSITIVITY],
                               bass[BASS_GATE],
                               bass[BASS_KICK],
                               bass[BASS_DECAY],
                               bass[BASS_BAND],
                               bass[BASS_STYLE],
                               bass[BASS_FLAGS],
                               bass[BASS_HUM]};
  static_assert(sizeof(payload) <= 20, "state no longer fits one default-MTU notification");
  gStateChar->setValue(payload, sizeof(payload));
  gStateChar->notify();
  gLastPushAt = millis();
}

// Pocket mode, on its own characteristic because the state payload is full.
// One byte, so a client only has to read bit 0 of byte 0 to know.
void pushScreen(bool force) {
  if (gScreenChar == nullptr) {
    return;
  }
  const bool on = Display::on();
  if (!force && gLastScreenValid && on == gLastScreen) {
    return;
  }
  gLastScreen = on;
  gLastScreenValid = true;
  const uint8_t payload[1] = {(uint8_t)(on ? 1 : 0)};
  gScreenChar->setValue(payload, sizeof(payload));
  gScreenChar->notify();
}

void pushMeter() {
  if (gMeterChar == nullptr || !Leds::bassLive() || gMeterChar->getSubscribedCount() == 0) {
    return;
  }
  const uint32_t now = millis();
  if (now - gLastMeterAt < kMeterMinMs) {
    return;
  }
  gLastMeterAt = now;
  const BassDsp::Meter m = Leds::bassMeter();
  const uint8_t payload[4] = {m.level, m.trigger, m.out, m.kicks};
  gMeterChar->setValue(payload, sizeof(payload));
  gMeterChar->notify();
}

}  // namespace

void begin() {
  char name[32];
  uint64_t chip = ESP.getEfuseMac();
  snprintf(name, sizeof(name), "Helmet-LED-%04X", (unsigned)(chip & 0xFFFF));

  NimBLEDevice::init(name);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // Everything but an update fits the default 23, but a firmware image at 20
  // payload bytes a packet would take the best part of an hour. The phone
  // negotiates down if it cannot manage this.
  NimBLEDevice::setMTU(517);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&gServerCallbacks);
  gServer = server;

  NimBLEService *service = server->createService(kServiceUuid);

  NimBLECharacteristic *control = service->createCharacteristic(
      kControlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  control->setCallbacks(&gControlCallbacks);

  gStateChar = service->createCharacteristic(
      kStateUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  gMeterChar = service->createCharacteristic(
      kMeterUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  gScreenChar = service->createCharacteristic(
      kScreenUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  gOtaCtrlChar = service->createCharacteristic(
      kOtaCtrlUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  gOtaCtrlChar->setCallbacks(&gOtaCtrlCallbacks);

  // Write without response: an acknowledgement per chunk would halve the
  // throughput an image needs, and the progress notifications already tell
  // the app how far behind the helmet is.
  NimBLECharacteristic *otaData = service->createCharacteristic(
      kOtaDataUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  otaData->setCallbacks(&gOtaDataCallbacks);

  service->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setAppearance(0x0080);  // generic light fixture
  advertising->start();

  pushState(true);
  pushScreen(true);
}

void loop() {
  if (gOtaRebootPending) {
    // The image is committed and the app has been told. Give the notification
    // a moment to go out, then come up on the new slot.
    if (millis() - gOtaRebootAt >= 400) {
      Ota::reboot();
    }
    return;
  }
  if (Ota::active()) {
    // Nothing else goes out while an image is arriving: the state and meter
    // notifications would be spending airtime the transfer needs.
    if (millis() - gLastOtaAt >= kOtaProgressMs) {
      pushOta();
    }
    return;
  }
  pushState(false);
  pushScreen(false);
  pushMeter();
}

bool connected() { return gServer != nullptr && gServer->getConnectedCount() > 0; }

}  // namespace Ble
