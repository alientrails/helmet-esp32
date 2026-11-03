# ESP32 Helmet LED Controller

Replaces the stock helmet LEDs with a **WS2812B** (3-wire) strip on a
**Seeed XIAO ESP32-S3**. Power is a USB power bank into USB-C. A 0.91" I2C
OLED, three buttons, and a MAX9814 mic are the controls.

Controllable from the [Android app](android/README.md) over Bluetooth LE —
no WiFi needed.

This firmware drives **individually addressable** 5 V strips (WS2812B /
BTF-LIGHTING ECO). It does **not** drive a 2-wire MOSFET strip.

## Cut the reel first

The Amazon 16.4 ft / 300 LED reel is far too long for a helmet and far too
hungry for USB.

- Cut only on the copper pads between pixels, following the printed arrows.
- Helmet rim is usually ~0.8–1.5 m. At 60 LEDs/m that is about **50–90
  pixels**.
- Count the pixels you keep and set `LED_COUNT` in `include/config.h`
  (default **60**).
- Full white at full brightness is ~60 mA per pixel. Firmware caps draw at
  `LED_MAX_MA` (1000 mA). Do not power the uncut 5 m reel from the XIAO.

## XIAO pin map

USB-C at the top. **5V** is on the back, upper right, across from D0.

```
              USB-C
         D0  Mic OUT     5V     → strip 5V
         D1  Left        GND    → strip GND
         D2  Right       3V3    → OLED VCC, mic VCC
         D3  Select      D10    → strip DIN (via 330 Ω)
         D4  OLED SDA    D9
         D5  OLED SCL    D8
         D6              D7
```

## Wiring (3-wire WS2812B)

Strip pads are usually **5V / DIN / GND** (sometimes a 3-pin JST). Data
arrow on the strip must point **away** from the XIAO.

```
 USB power bank ---- USB-C on XIAO

 Strip 5V  ---- XIAO 5V          (back of board, next to USB)
 Strip GND ---- XIAO GND
 Strip DIN ---- 330 Ω ---- XIAO D10

 OLED VCC ---- 3V3
 OLED SDA ---- D4
 OLED SCL ---- D5

 Left   button: D1 -- switch -- GND
 Right  button: D2 -- switch -- GND
 Select button: D3 -- switch -- GND

 MAX9814 — 3.3 V only:
   VCC ---- 3V3
   GND ---- GND
   OUT ---- D0
```

No MOSFET. D10 is a **data** line, not a power switch.

If you use more than ~30 LEDs, feed strip 5V from a USB-C pigtail on the
power bank, not through the tiny XIAO 5V pad. GND still common with the XIAO.

## Parts

| Qty | Part | Notes |
| --- | --- | --- |
| 1 | Seeed **XIAO ESP32-S3** | Recommended. C3: `-e xiao_c3` |
| 1 | WS2812B 5 V strip | Cut to helmet length. 60 LEDs/m is fine |
| 1 | 330 Ω resistor | in series on DIN |
| 1 | 0.91" I2C OLED SSD1306 128×32 | VCC to 3V3 |
| 3 | 6×6 mm tactile buttons | left, right, select |
| 1 | MAX9814 mic amp | 3.3 V. Do not power from 5 V |
| 1 | USB power bank | “always on” if it sleeps at low current |
| — | 22–24 AWG, heat shrink | |

## Build order

1. Flash the XIAO (`xiao_s3`). OLED can still say `OLED not found`.
2. Wire OLED + buttons. Confirm the **HELMET** splash and the menu.
3. Cut the strip, set `LED_COUNT`, reflash. Wire 5V, GND, DIN→D10 via 330 Ω.
   Select **Solid**.
4. Mic on D0. Select **Bass** and clap.
5. Mount. Point the mic at the visor, not into foam.

## Flash

```bash
cd helmet-esp32
pio run -e xiao_s3 -t upload
```

Hold **BOOT**, tap **RST**, release BOOT, then upload. Merged images
(address `0x0`): `bin/firmware-xiao-s3-merged.bin` (chip ESP32-S3),
`bin/firmware-xiao-c3-merged.bin` (ESP32-C3).

## Use

| Control | Action |
| --- | --- |
| Any button from home | open menu |
| Left / right | scroll modes, Brightness, Lights |
| Select on a mode | run it |
| Select on Brightness | left/right change level |
| Select on Lights | toggle |
| Long-press select | home, or toggle from home |

Modes: Solid, Breathe, Heartbeat, Slow blink, Fast blink, Strobe, SOS,
Party (rainbow), **Bass** (purple kick meter).

**Bass** uses the MAX9814 on D0. Brightness is output gain. MAX9814 GAIN
floating = 50 dB; to 3V3 = 40 dB (loud club); to GND = 60 dB (quiet).

If colors look swapped (red/green), the strip is not GRB — say so and we
can switch the color order.

If the OLED is upside down, set `OLED_ROTATION` to `2` in `include/config.h`.

## CYD touchscreen build

There is a second hardware target: the **ESP32-2432S028R "Cheap Yellow
Display"** — a classic ESP32 with a 2.8" 320x240 ILI9341 panel and a
resistive XPT2046 touchscreen. It replaces the OLED and the three buttons
with a full touch UI, and runs the same LED engine, BLE service and
persistence as the XIAO builds.

```bash
pio run -e cyd -t upload
```

Wire the strip to the **P3/CN1** header: `DIN` to GPIO22 (via 330 Ohm),
plus 5V and GND. The mic goes to GPIO35.

The interface is built with [LVGL](https://lvgl.io) on top of TFT_eSPI and
wears the Alien Trails Labs identity (see [`brand/`](brand/README.md)):

- **Boot** — the Alien Trails Labs mark pops in, the wordmark follows and a
  status line types out; it holds for `LOGO_SPLASH_MS` (2.5 s) and fades to
  the controls.
- **Top bar** — the mark, the running mode, a BLE signal tag (`BLE` ring while
  advertising, `LINK` disc when a phone is connected) and the power switch.
  A hairline meter underneath shows the live PWM level in the strip's own
  colour, so you can see a pattern running without looking up.
- **Brightness** — always visible: a dotted phosphor-to-ion trail that ends in
  the draggable ion signal dot.
- **Modes** — the nine patterns as a 3x3 grid; the live one is tinted phosphor.
- **Colour** — a hue ring with the current colour and its hex in the middle,
  plus twelve presets including a warm and a neutral white.
- **System** — firmware, strip, BLE and uptime, and a touch recalibration
  button.

Touch is calibrated on first boot (three targets) and the affine solution is
stored in NVS, so any panel orientation works without hard-coded constants.
Press `c` over serial to redo it, `t` to dump raw touch samples.

### Previewing the UI without a board

`src/ui_screens.cpp` is deliberately free of Arduino and TFT_eSPI, so the
exact shipping layout can be rendered on a desktop:

```bash
pio pkg install -e cyd
make -C tools/sim
./tools/sim/build/preview /tmp/shots
```

That writes one PPM per frame (the boot splash timeline, Modes, Colour,
System). Handy for
iterating on spacing and colour without a flash cycle.

## Android app

The firmware advertises as `Helmet-LED-XXXX` over BLE once flashed. Build and
install the app from [`android/`](android/README.md) to control mode,
brightness, and on/off from a phone — see that folder's README for the GATT
protocol details.
