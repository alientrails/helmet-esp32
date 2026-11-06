# TODO

## Android app

- [x] **Build it.** It compiled clean on the first pass with no source
      changes; 20 protocol unit tests now pass and the debug APK builds.
- [ ] Check it on a phone against the firmware: the boot card (mark pops in
      from a bare void, no mark in the system splash first), the trail and ion
      slider drags, the hue ring, and the bass meter moving in Bass mode.
- [ ] Check it against older firmware too: sections the helmet does not
      report (bass, skip) should simply not appear.

## Pocket mode (screen off)

- [ ] Check it on hardware: the CYD's backlight fading down, touch dead while
      it is off, and the panel coming back up showing current state rather
      than whatever was on it when it went dark.
- [ ] Check it survives a reboot in the pocket — the helmet should come up
      dark with no boot card, and the app's switch should come up off.
- [ ] Check the XIAO half, which is written but has only been compiled: the
      OLED powering down, the buttons dropped, and no spurious click when the
      screen comes back on after a button was held through it.
- [ ] A kick arriving while the screen is off keeps the bass detector running
      and the strip flashing, which is intended, but it has not been watched.

## Firmware

- [ ] Rebuild the merged images in `bin/` — they date from before the CYD and
      LVGL commits. Add a `bin/firmware-cyd-merged.bin` too.
- [ ] Verify the power limiting on a real strip with a meter, now that the
      budget is a runtime slider rather than `LED_MAX_MA` alone.
- [ ] The XIAO OLED menu has no speed entry, so speed is only reachable from
      the CYD or over BLE on those builds.
- [ ] Bass on the XIAO builds is compiled and shares the CYD's code, but has
      only been run on the CYD. Check the 1 kHz sampler's cost there (`b` on
      the serial console prints it) and that the direct ADC1 read returns the
      same values as `analogRead()`.
- [ ] The BLE state is exactly 20 bytes now, the most one notification carries
      at the default MTU. The next field needs the central to negotiate a
      larger MTU, or a characteristic of its own like the bass meter. This is
      why the Bass rainbow went in as a flag bit rather than a new BassParam.
- [ ] `fastled/FastLED@^3.9.13` resolves to 3.10.9, whose ESP32-C3 PARLIO
      driver does not link: `pio run -e xiao_c3` fails with undefined
      references to `fl::detail::ParlioEngine` inside FastLED itself. The cyd
      and xiao_s3 builds are fine. Pinning the version exactly would fix it,
      but it changes what every board builds against, so it wants testing on
      hardware rather than a blind bump.

## Firmware update (OTA)

- [ ] **One USB upload per helmet.** The CYD moved from `huge_app` to
      `min_spiffs` for its second app slot, and a partition change cannot
      arrive over the air. Flash each helmet once over USB on 1.9.0 or newer,
      and after that the app can do it.
- [ ] Try a real update end to end, including the things that should fail
      safely: cancel halfway, walk out of range halfway, and send a file that
      is not firmware. All three should leave the helmet on its old image.
- [ ] Time a full image over BLE and see whether the 32 KB window and the
      250 ms progress interval are the right numbers on real hardware.
- [ ] There is no automatic rollback: Arduino's stock bootloader does not
      enable the IDF's, so an image that is intact but broken still needs a
      USB recovery. Worth considering `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
      if updates become routine.

## CYD build

- [ ] Test on hardware end to end: touch calibration, all sixteen modes, the
      speed control, colour ring, BLE link indicator.
- [ ] Check the Alien Trails splash and Space Grotesk/Mono rendering on the
      real panel (RGB565 dithering on the logo, colour order of the TFT).
- [ ] Check the seven new patterns on a real strip — they were verified for
      arithmetic (no overflow at 1..120 px, heads reach both ends) but never
      actually looked at. Fire and Twinkle in particular are tuned by eye.
- [ ] Document the CYD pinout (strip, mic) with a diagram like the XIAO one.
- [ ] Tie the MAX9814's GAIN pin to 3V3 (40 dB) or GND (50 dB). Floating is
      60 dB, the noisiest setting; see the Bass notes in the README. Then
      re-check the noise gate against the meter.
- [ ] Try Bass against a real bass speaker. So far it has only heard a phone
      speaker, which cannot reproduce a kick's 45-110 Hz fundamental at all.

## Housekeeping

- [ ] Add a CI job that runs `pio run` for `xiao_s3`, `xiao_c3` and `cyd`, plus
      the `tools/sim` preview build and `tools/bass_test`.
