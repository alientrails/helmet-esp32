#include <Arduino.h>

#include "ble.h"
#include "config.h"
#include "leds.h"
#include "persist.h"
#include "ui_touch.h"

static void handleSerial() {
  if (!Serial.available()) {
    return;
  }
  const char c = (char)Serial.read();
  if (c == 's' || c == 'S') {
    uint8_t r, g, b;
    Leds::color(&r, &g, &b);
    Serial.printf("on=%d mode=%s bri=%u color=%u,%u,%u speed=%u%% %s %upx %umA\n",
                  Leds::on(), Leds::modeName(), Leds::brightness(), r, g, b,
                  Leds::speedPercent(), Leds::orderName(), Leds::count(),
                  Leds::maxMilliamps());
  } else if (c == 'm' || c == 'M') {
    Serial.printf("mic pin %d raw:", PIN_MIC);
    for (int i = 0; i < 20; i++) {
      Serial.printf(" %d", analogRead(PIN_MIC));
      delay(15);
    }
    Serial.println();
  } else if (c == 't' || c == 'T') {
    UiTouch::printTouchDebug();
  } else if (c == 'c' || c == 'C') {
    UiTouch::calibrate();
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nhelmet-esp32 %s  CYD  touchscreen\n", HELMET_FW_VERSION);
  Serial.printf("WS2812 GPIO%d  count %d  max %d  mic GPIO%d\n", PIN_LED_DATA,
                LED_COUNT, LED_COUNT_MAX, PIN_MIC);

  Leds::begin();
  Persist::begin();
  UiTouch::begin();
  Ble::begin();
}

void loop() {
  handleSerial();
  Ble::loop();
  Leds::loop();
  UiTouch::loop();
  Persist::loop();
  delay(Leds::mode() == MODE_BASS ? 0 : 2);
}
