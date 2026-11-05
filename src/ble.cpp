#include "ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "config.h"
#include "leds.h"

namespace Ble {
namespace {

constexpr char kServiceUuid[] = "7b1e8f00-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kControlUuid[] = "7b1e8f01-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kStateUuid[] = "7b1e8f02-2a5d-4e1a-9c3a-2f6b1d8a10e0";
constexpr char kMeterUuid[] = "7b1e8f03-2a5d-4e1a-9c3a-2f6b1d8a10e0";

// The state is a set of settings plus the live output level. Settings go out
// the moment they change. The level on its own changes every frame in any
// animated mode, and sending it every frame used to flood the link with
// hundreds of notifications a second, so level-only updates are capped.
constexpr uint32_t kLevelMinMs = 100;  // 10 Hz
// The bass meter is its own characteristic, streamed only while someone is
// subscribed and the detector is running. 25 Hz reads as live on a meter.
constexpr uint32_t kMeterMinMs = 40;

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
};

NimBLEServer *gServer = nullptr;
NimBLECharacteristic *gStateChar = nullptr;
NimBLECharacteristic *gMeterChar = nullptr;
uint32_t gLastPushAt = 0;
uint32_t gLastMeterAt = 0;
uint8_t gLastBass[BASS_PARAM_COUNT] = {};
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

class ServerCallbacks : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer *server) override {
    NimBLEDevice::startAdvertising();
  }
};

ControlCallbacks gControlCallbacks;
ServerCallbacks gServerCallbacks;

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

  service->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setAppearance(0x0080);  // generic light fixture
  advertising->start();

  pushState(true);
}

void loop() {
  pushState(false);
  pushMeter();
}

bool connected() { return gServer != nullptr && gServer->getConnectedCount() > 0; }

}  // namespace Ble
