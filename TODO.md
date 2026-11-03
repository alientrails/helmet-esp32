# TODO

## Android app

- [ ] Add colour control — firmware already handles `SET_COLOR` (0x06), but
      `HelmetBle.kt` only defines ops 0x01–0x05. Add the opcode, a
      `setColor(r, g, b)` call, and a hue picker/presets to match the CYD UI.
- [ ] Show the current colour from the state notification.
- [ ] Update `android/README.md` protocol table with `SET_COLOR`.
- [ ] Build the Alien Trails redesign in Android Studio and check it on a
      phone: launch splash timing, the trail slider drag, adaptive icon.

## Firmware

- [ ] Rebuild the merged images in `bin/` — they date from before the CYD and
      LVGL commits. Add a `bin/firmware-cyd-merged.bin` too.
- [ ] Color order option (GRB vs RGB) in `include/config.h` instead of asking
      in the README.
- [ ] Verify `LED_MAX_MA` power limiting on a real strip with a meter.

## CYD build

- [ ] Test on hardware end to end: touch calibration, all nine modes, colour
      ring, BLE link indicator.
- [ ] Check the Alien Trails splash and Space Grotesk/Mono rendering on the
      real panel (RGB565 dithering on the logo, colour order of the TFT).
- [ ] Document the CYD pinout (strip, mic) with a diagram like the XIAO one.

## Housekeeping

- [ ] Add a CI job that runs `pio run` for `xiao_s3`, `xiao_c3` and `cyd`, plus
      the `tools/sim` preview build.
- [ ] Remove the stray untracked `.tmp` file in the repo root.
