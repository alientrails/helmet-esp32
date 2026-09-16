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

enum Opcode : uint8_t {
  OP_SET_MODE = 0x01,
  OP_SET_BRIGHTNESS = 0x02,
  OP_SET_ON = 0x03,
  OP_TOGGLE = 0x04,
  OP_NUDGE_BRIGHTNESS = 0x05,
  OP_SET_COLOR = 0x06,
};

NimBLEServer *gServer = nullptr;
NimBLECharacteristic *gStateChar = nullptr;
bool gDirty = false;
uint8_t gLastMode = 0xFF;
uint8_t gLastBri = 0xFF;
bool gLastOn = true;
uint8_t gLastLevel = 0xFF;
uint8_t gLastR = 0xFF;
uint8_t gLastG = 0xFF;
uint8_t gLastB = 0xFF;

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

  if (!force && mode == gLastMode && bri == gLastBri && on == gLastOn &&
      level == gLastLevel && r == gLastR && g == gLastG && b == gLastB) {
    return;
  }
  gLastMode = mode;
  gLastBri = bri;
  gLastOn = on;
  gLastLevel = level;
  gLastR = r;
  gLastG = g;
  gLastB = b;

  const uint8_t payload[7] = {mode, bri, (uint8_t)(on ? 1 : 0), level, r, g, b};
  gStateChar->setValue(payload, sizeof(payload));
  gStateChar->notify();
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

  service->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setAppearance(0x0080);  // generic light fixture
  advertising->start();

  pushState(true);
}

void loop() { pushState(false); }

bool connected() { return gServer != nullptr && gServer->getConnectedCount() > 0; }

}  // namespace Ble
