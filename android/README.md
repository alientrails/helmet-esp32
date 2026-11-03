# Helmet LED — Android app

Controls the helmet over **Bluetooth LE**. No WiFi network needed — the app
scans for the ESP32, connects directly, and talks to the GATT service defined
in `../include/ble.h`.

## Build

1. Open the `android/` folder in Android Studio (Koala or newer). It will
   fetch the Gradle wrapper and SDK components on first sync.
2. Run on a device with Android 8.0 (API 26) or newer, Bluetooth LE capable.
3. Or from the command line, after Android Studio has generated
   `gradlew`/`gradle-wrapper.jar` once:
   ```bash
   cd android
   ./gradlew assembleDebug
   ```
   The APK lands in `app/build/outputs/apk/debug/`.

## Look and feel

The app uses the Alien Trails Labs design system (see
[`../brand/`](../brand/README.md)): void ground, Space Grotesk and Space Mono,
phosphor for what is live, and the dotted ion trail as the brightness slider.
On launch the Alien Trails Labs logo shows for about 2.5 seconds, then fades
to the controls. With system animations turned off it shows the end state with
no motion. The theme lives in `ui/theme/Theme.kt`; shared pieces (SignalTag,
TrailSlider, ModeGrid, buttons) in `ui/AtComponents.kt`; the boot card in
`ui/BrandSplash.kt`.

## Use

1. Flash the firmware (`pio run -e xiao_s3 -t upload` from the repo root) —
   it now advertises as `Helmet-LED-XXXX` over BLE.
2. Open the app, tap **Connect**. Grant the Bluetooth permission prompt
   (and enable Bluetooth if asked).
3. Toggle lights, drag brightness, tap a mode. Changes apply immediately;
   the firmware still saves the last mode/brightness/on-state to flash after
   a moment, same as with the physical buttons.
4. Physical button changes on the helmet show up in the app too — the ESP32
   pushes state updates over a notify characteristic.

## Protocol

See `../include/ble.h` for the exact GATT layout (one write characteristic
for commands, one notify characteristic for live state). `HelmetBle.kt` and
`HelmetProtocol` are the Android-side mirror — keep both in sync if the
firmware protocol changes, in particular `HelmetProtocol.MODE_NAMES`, which
must match `kNames` in `../src/leds.cpp` index-for-index.
