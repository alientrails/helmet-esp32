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
4. The protocol is covered by JVM unit tests, which need no device:
   ```bash
   ./gradlew testDebugUnitTest
   ```

## Pocket mode: switching the helmet's screen off

**Screen**, in the card under the power switch, blanks the helmet's own
display and makes it ignore its touchscreen or buttons. It is for the obvious
reason: the controller rides in a pocket, where a touchscreen is a row of
buttons being pressed by a thigh.

Everything else carries on — the lights keep running, the mic keeps listening,
this link stays up — so the way back is always in the app. And it is *only* in
the app, in both directions, because a screen the helmet could switch back on
would be switched back on by being sat on. The helmet saves the state, so it
stays off through a reboot and comes up with no boot card at all.

The control only appears against firmware 1.10.0 and newer, which is where the
screen characteristic (`7b1e8f06-…`) comes from; older helmets show no switch
rather than one that would do nothing.

## Updating the helmet's firmware

The **System** section offers *Update firmware* when the connected helmet
carries the update characteristics (firmware 1.9.0 and newer on the CYD).
Pick a `.bin` built for that board — `pio run -e cyd` leaves one in
`.pio/build/cyd/firmware.bin` — and the app checks it looks like an ESP32
image, then streams it over BLE in MTU-sized chunks.

The app never gets more than 32 KB ahead of what the helmet has confirmed
receiving, so a helmet busy writing flash slows the phone down rather than
losing the middle of the image. Progress is reported from what the helmet has
acknowledged, not from what the phone has queued. Expect one to three minutes:
BLE is slow, and the alternative was asking the phone to join a Wi-Fi network
and give up its internet for the duration.

Nothing is switched over until the whole image has arrived and its CRC-32
matches, so cancelling, walking out of range or a flat battery all leave the
helmet on the firmware it was already running.

## Look and feel

The app uses the Alien Trails Labs design system (see
[`../brand/`](../brand/README.md)): void ground, Space Grotesk and Space Mono,
phosphor for what is live, and the dotted ion trail as the brightness slider.
On launch the Alien Trails Labs logo shows for about 2.5 seconds, then fades
to the controls — the same boot card as the CYD, with the same timing: the
mark pops in from a bare void, the wordmark follows, and the status line
types out near the bottom edge. On Android 12+ the system splash is kept a
blank void so the mark is not shown twice. With system animations turned off
it shows the end state with no motion. The launcher icon is the Alien Trails
mark on void.

The theme lives in `ui/theme/Theme.kt`; shared pieces (SignalTag,
TrailSlider, ModeGrid, buttons) in `ui/AtComponents.kt`; the controls added
for parity with the CYD (ion sliders, option rows, hue ring, presets, bass
meter) in `ui/AtControls.kt`; the page sections in `ui/Sections.kt`; the boot
card in `ui/BrandSplash.kt`.

## Use

1. Flash the firmware (`pio run -e xiao_s3 -t upload` from the repo root) —
   it now advertises as `Helmet-LED-XXXX` over BLE.
2. Open the app, tap **Connect**. Grant the Bluetooth permission prompt
   (and enable Bluetooth if asked).
3. Everything the CYD touchscreen can do is on one scrolling page, in the
   CYD's tab order: power, brightness and speed; the sixteen modes; colour
   (hue ring and the CYD's twelve presets); Bass; the strip's colour order,
   pixel count, hidden pixels and current budget; and system info. Changes
   apply as you drag, and the helmet saves them to flash once they have held
   still for a moment.
4. **Bass calibration.** In Bass mode the input meter is live: the bar is the
   mic's level, the amber line the noise gate, the ink notches the level a
   kick has to clear, and the bar flashes phosphor when one fires. With
   nothing playing, set **Noise gate** just above where the bar settles; then
   adjust **Sensitivity** and **Kick** with music on. **Hum** notches out
   mains hum and is best left on.
5. **Screen.** The switch under the power row is pocket mode — see above.
   It is the only way to turn the helmet's screen off, and the only way to
   turn it back on.
6. Changes made on the helmet show up in the app too — the ESP32 pushes
   state updates over a notify characteristic.

## Protocol

See `../include/ble.h` for the exact GATT layout: one write characteristic
for commands, one notify characteristic for the state (20 bytes), one for the
live bass meter (4 bytes, streamed only in Bass mode) and one for pocket mode
(1 byte). Pocket mode has a characteristic of its own because the state
payload is already exactly the 20 bytes one notification carries at the
default MTU — and its presence doubles as the capability flag. `HelmetProtocol.kt`
is the Android-side mirror and holds all the byte layout as pure functions,
which `app/src/test` pins down; `HelmetBle.kt` does the Bluetooth.

Keep the two sides in sync when the firmware protocol changes — in particular
`MODE_NAMES` (must match `kNames` in `../src/leds.cpp` index for index), the
`BASS_*` ids (`BassParam` in `../include/leds.h`), and `decayMs()`
(`BassDsp::decayMs()`). The state payload only grows at the end, and the app
hides any control whose field an older helmet did not send.

`HelmetBle` queues every GATT operation, because Android runs only one at a
time per connection and fails the rest silently. Writes to the same control
coalesce, so a slider drag never builds a backlog.
