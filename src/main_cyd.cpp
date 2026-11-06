#include <Arduino.h>

#include "ble.h"
#include "config.h"
#include "display.h"
#include "leds.h"
#include "persist.h"
#include "ui_touch.h"

// Bass calibration from the serial console. 'b' streams the detector's meter
// at 20 Hz; 'r' captures two seconds of the exact 1 kHz samples it sees, for
// looking at the mic's noise on a host. Both need Bass mode, because that is
// the only time the sampler runs.
constexpr uint16_t kCaptureLen = 2000;
static uint16_t gCapture[kCaptureLen];
static bool gCapturing = false;
static bool gBassStream = false;
static uint32_t gBassStreamAt = 0;

static void bassTelemetry() {
  if (gCapturing && Leds::bassCaptureDone()) {
    gCapturing = false;
    // ~10 KB at 115200 baud: the loop, and so the strip, stalls for about a
    // second while it goes out. Fine for a console tool.
    Serial.printf("RAW %u Hz %u:", (unsigned)BassDsp::kRateHz, (unsigned)kCaptureLen);
    for (uint16_t i = 0; i < kCaptureLen; i++) {
      Serial.printf(" %u", gCapture[i]);
    }
    Serial.println();
  }
  if (!gBassStream) {
    return;
  }
  const uint32_t now = millis();
  if (now - gBassStreamAt < 50) {
    return;
  }
  gBassStreamAt = now;
  const BassDsp::Meter m = Leds::bassMeter();
  Serial.printf("B level %3u trig %3u gate %3u out %3u kicks %3u cost %uus%s\n", m.level,
                m.trigger, Leds::bass(BASS_GATE), m.out, m.kicks, Leds::bassCostUs(),
                Leds::bassLive() ? "" : "  (idle: not in Bass mode)");
}

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
    Serial.printf("screen: %s\n", Display::on() ? "on" : "off (pocket mode)");
    Serial.printf("bass: sens %u gate %u kick %u decay %u band %u style %u flags 0x%02x\n",
                  Leds::bass(BASS_SENSITIVITY), Leds::bass(BASS_GATE), Leds::bass(BASS_KICK),
                  Leds::bass(BASS_DECAY), Leds::bass(BASS_BAND), Leds::bass(BASS_STYLE),
                  Leds::bass(BASS_FLAGS));
  } else if (c == 'm' || c == 'M') {
    Serial.printf("mic pin %d raw:", PIN_MIC);
    for (int i = 0; i < 20; i++) {
      Serial.printf(" %d", analogRead(PIN_MIC));
      delay(15);
    }
    Serial.println();
  } else if (c == 'b' || c == 'B') {
    gBassStream = !gBassStream;
    Serial.printf("bass telemetry %s%s\n", gBassStream ? "on" : "off",
                  Leds::bassLive() ? "" : " -- not in Bass mode, so the detector is idle");
  } else if (c == 'r' || c == 'R') {
    if (!Leds::bassLive()) {
      Serial.println("raw capture needs Bass mode: the sampler only runs there");
    } else {
      Leds::bassCapture(gCapture, kCaptureLen);
      gCapturing = true;
      Serial.println("capturing 2 s of 1 kHz mic samples...");
    }
  } else if (c == 'd' || c == 'D') {
    // The escape hatch for pocket mode, which the app is otherwise the only
    // way out of. A USB cable is the one thing a pocket cannot press.
    Display::setOn(!Display::on());
    Persist::markDirty();
    Serial.printf("screen %s\n", Display::on() ? "on" : "off (pocket mode)");
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

  // After Persist, so this reports the geometry actually in force rather than
  // the compile-time defaults printed above -- which is the only way to tell
  // from the log whether a saved skip or count survived the update.
  Serial.printf("strip: %u px  %u hidden  %u lit  %u mA  %s\n",
                (unsigned)Leds::count(), (unsigned)Leds::skip(),
                (unsigned)Leds::lit(), (unsigned)Leds::maxMilliamps(),
                Leds::orderName());
  UiTouch::begin();
  Ble::begin();
}

void loop() {
  handleSerial();
  bassTelemetry();
  Ble::loop();
  Leds::loop();
  UiTouch::loop();
  Persist::loop();
  delay(Leds::mode() == MODE_BASS ? 0 : 2);
}
