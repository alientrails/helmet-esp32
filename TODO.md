# TODO

## Android app

- [ ] **Build it.** Version 1.1 was written without a compiler pass: speed,
      colour, bass (with the live meter), strip settings and a GATT queue are
      all new, none of it has been through Gradle. Run
      `./gradlew testDebugUnitTest assembleDebug` in `android/` and fix
      whatever it finds; the unit tests pin the byte layout to the firmware.
- [ ] Check it on a phone against 1.7.0 firmware: the boot card (mark pops in
      from a bare void, no mark in the system splash first), the trail and ion
      slider drags, the hue ring, and the bass meter moving in Bass mode.
- [ ] Check it against older firmware too: sections the helmet does not
      report (bass, skip) should simply not appear.

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
      larger MTU, or a characteristic of its own like the bass meter.

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
