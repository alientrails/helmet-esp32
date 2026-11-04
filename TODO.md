# TODO

## Android app

- [ ] Add colour control — `HelmetBle.kt` now defines `OP_SET_COLOR` but has no
      `setColor(r, g, b)` call or hue picker/presets to match the CYD UI.
- [ ] Show the current colour from the state notification.
- [ ] Update `android/README.md` protocol table with `SET_COLOR`.
- [ ] Build the Alien Trails redesign in Android Studio and check it on a
      phone: launch splash timing, the trail slider drag, adaptive icon.
- [ ] Add speed, colour order, pixel count and current budget controls —
      opcodes `0x07`–`0x0A` are defined in `HelmetProtocol` but unused, and
      `HelmetState.speed` is parsed but never shown.

## Firmware

- [ ] Rebuild the merged images in `bin/` — they date from before the CYD and
      LVGL commits. Add a `bin/firmware-cyd-merged.bin` too.
- [ ] Verify the power limiting on a real strip with a meter, now that the
      budget is a runtime slider rather than `LED_MAX_MA` alone.
- [ ] The XIAO OLED menu has no speed entry, so speed is only reachable from
      the CYD or over BLE on those builds.

## CYD build

- [ ] Test on hardware end to end: touch calibration, all sixteen modes, the
      speed control, colour ring, BLE link indicator.
- [ ] Check the Alien Trails splash and Space Grotesk/Mono rendering on the
      real panel (RGB565 dithering on the logo, colour order of the TFT).
- [ ] Check the seven new patterns on a real strip — they were verified for
      arithmetic (no overflow at 1..120 px, heads reach both ends) but never
      actually looked at. Fire and Twinkle in particular are tuned by eye.
- [ ] Check the hidden-LED skip on a real strip — the host preview stubs out
      `Leds`, so `writeWire()` has never actually run. Confirm the first
      LED_SKIP pixels stay dark and that a comet sweeps the whole visible arc
      rather than vanishing early, then set HIDDEN by eye on the assembled
      helmet. The boot log line (`strip: ... hidden ... lit`) reports what is
      in force.
- [ ] Document the CYD pinout (strip, mic) with a diagram like the XIAO one.

## Housekeeping

- [ ] Add a CI job that runs `pio run` for `xiao_s3`, `xiao_c3` and `cyd`, plus
      the `tools/sim` preview build.
- [ ] Remove the stray untracked `.tmp` file in the repo root.
