# ESP32 Helmet LED Controller

Replaces the stock AAA blink box on an existing two-wire LED helmet.
A **Seeed XIAO ESP32-S3** (C3 also supported) PWM-switches the LEDs through
a MOSFET. Power is a **USB power bank** into the XIAO USB-C port. A **0.91"
I2C OLED**, three buttons, and a **MAX9814** mic are the controls.

Phone web UI, iPhone captive portal, and Android app are next — not in this
build.

Works with ordinary 2-wire LED helmets (POS / NEG clip). If you later find a
third **DATA** wire, this is the wrong firmware — use WLED instead.

## XIAO pin map

USB-C at the top, pads facing you:

```
              USB-C
         D0  Mic OUT     5V     → helmet POS (LED +)
         D1  Left        GND    → common ground
         D2  Right       3V3    → OLED VCC, mic VCC
         D3  Select      D10    → MOSFET gate (via 100 Ω)
         D4  OLED SDA    D9
         D5  OLED SCL    D8
         D6              D7
```

Leave the **BAT+ / BAT−** pads empty for now. They are the XIAO’s built-in
LiPo charger if you add a 1S cell later. Do not use BAT and a power bank as
two live sources without meaning to charge the cell.

## Wiring

```
 USB power bank ---- USB-C on XIAO

 XIAO 5V  ------------------------- helmet POS clip  (LED +)
 XIAO GND ----+------------------- MOSFET SOURCE (IRLZ44N pin 3)
              |
              +------------------- OLED GND, button GND

 helmet NEG clip ----------------- MOSFET DRAIN (center pin / tab)

 XIAO D10 ---- 100 Ω ---- MOSFET GATE (IRLZ44N pin 1)
                             |
                            10 kΩ
                             |
                            GND

 OLED VCC ---- 3V3
 OLED SDA ---- D4
 OLED SCL ---- D5

 Left   button: D1 -- switch -- GND
 Right  button: D2 -- switch -- GND
 Select button: D3 -- switch -- GND

 MAX9814 (or MAX4466) mic amp — 3.3 V only:
   VCC ---- 3V3
   GND ---- GND
   OUT ---- D0
```

IRLZ44N, tab facing you, pins down: **Gate | Drain | Source**. The metal tab
is Drain.

Do **not** drive the helmet strip from a GPIO. The MOSFET is required.

If the strip pulls more than about 500 mA, do not feed LED+ through the XIAO
5V pad. Split USB 5V from the power bank (USB-C pigtail / breakout) to both
the XIAO USB-C **and** helmet POS, and share GND.

## Parts

| Qty | Part | Notes |
| --- | --- | --- |
| 1 | Seeed **XIAO ESP32-S3** | Recommended. USB-C + LiPo pads. C3 still works (`xiao_c3`) |
| 1 | 0.91" I2C OLED SSD1306 128×32 | 4-pin: VCC GND SCL SDA. Check the silk — order varies |
| 3 | 6×6 mm tactile buttons | left, right, select |
| 1 | IRLZ44N or AO3400 MOSFET | logic-level N-channel |
| 1 | 100 Ω + 1× 10 kΩ | gate resistor and pulldown |
| 1 | MAX9814 electret mic amp | 3.3 V. MAX4466 also works. Do not power from 5 V |
| 1 | USB power bank | prefer one that does **not** auto-off at low current |
| — | Heat shrink, 22–24 AWG | |

Cheap power banks sleep when the helmet lights are off (ESP32 current is
low). Use an “always on” / keep-alive bank, or one with a button that stays
on.

## Build order

1. Flash the XIAO. On the serial monitor you should see `OLED not found` until
   the screen is wired — that is OK.
2. Solder OLED (3V3, GND, D4 SDA, D5 SCL) and the three buttons. Confirm the
   splash **HELMET** and that left/right/select move the menu.
3. Solder MOSFET + helmet POS/NEG. Test LEDs from the same USB cable.
4. Solder the mic (3V3, GND, OUT→D0). Select **Bass** and clap — the helmet
   should flash. Play EDM; kicks should punch, hats should mostly stay out.
5. Move USB to the power bank and mount the board in the old battery bay.
   Point the mic toward the open visor / outside, not into foam.

## Flash

Prebuilt images (address `0x0`):
- `bin/firmware-xiao-s3-merged.bin` — chip **ESP32-S3**
- `bin/firmware-xiao-c3-merged.bin` — chip **ESP32-C3**

Chrome: [esptool-js](https://espressif.github.io/esptool-js/).

```bash
cd helmet-esp32
pio run -e xiao_s3 -t upload
```

First upload: hold **BOOT** (on the XIAO), tap **RST**, release BOOT, then
upload. C3: `-e xiao_c3`.

Arduino IDE: board **XIAO_ESP32S3** (or **XIAO_ESP32C3**), USB CDC On Boot
enabled, library **U8g2**. Pins are in `include/config.h`.

## Use

| Control | Action |
| --- | --- |
| Any button from the home screen | open menu |
| Left / right | scroll modes, Brightness, Lights |
| Select on a mode | run it (dot marks the active one) |
| Select on Brightness | left/right change level, select to go back |
| Select on Lights | toggle on/off |
| Long-press select | home, or toggle on/off from home |
| Hold left/right | repeat scroll / brightness |

Home screen shows the active mode, ON/OFF, and a brightness bar. Last mode
and brightness are stored in flash.

Modes: Solid, Breathe, Heartbeat, Slow blink, Fast blink, Strobe, SOS, Party,
**Bass**.

**Bass** listens on D0 and drives the LEDs from ~60–120 Hz kick/sub energy
(EDM bass). Hats and voice are filtered down. Brightness is output gain: turn
it up in a quiet room, down in a loud club. MAX9814 gain jumper: floating =
50 dB (good start), GAIN to 3V3 = 40 dB (loud venues), GAIN to GND = 60 dB
(quiet speakers). If it stays washed-out white, drop MAX9814 gain or
brightness. If kicks are weak, raise them.

The home-screen bar is live LED output, so in Bass it pumps with the music.

If the OLED is upside down, set `OLED_ROTATION` to `2` in `include/config.h`.
If the screen is blank, check 3V3 (not 5V), SDA/SCL swapped, and try
`OLED_ADDRESS 0x3D`. If you actually have a 0.96" 128×64, set `OLED_HEIGHT` to
`64`.

## Later

- iPhone: helmet Wi-Fi web page
- Android: native app
- Optional 1S LiPo on the XIAO BAT pads
