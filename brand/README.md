# Brand — Alien Trails Labs

The CYD touchscreen UI and the Android app both wear the **Alien Trails Labs**
identity: a near-black ground, a phosphor-green signal and a blue ion trail.
Everything in this folder is a master copied from the Alien Trails Labs design
system; nothing here is redrawn.

| Path | What |
| --- | --- |
| `logos/at-mark.png` | Primary mark, transparent |
| `logos/at-wordmark.png` | ALIEN TRAILS / LABS wordmark, transparent |
| `logos/at-mark-avatar.png` | Mark on void, 1024×1024 — launcher icon source |
| `fonts/SpaceGrotesk-Variable.woff2` | Space Grotesk 300–700 (OFL) |
| `fonts/SpaceMono-{Regular,Bold}.woff2` | Space Mono (OFL) |

## Regenerating derived assets

```bash
pip install fonttools brotli pillow
npm i -g lv_font_conv@1.5.2
python3 tools/brand/generate.py
```

That rebuilds:

- `src/brand/at_*.c` — LVGL bitmap fonts (Space Grotesk 13/14/20, Space Mono
  11/13) and RGB565 logo rasters for the CYD.
- `android/app/src/main/res/font/` — static Space Grotesk and Space Mono TTFs.
- `android/app/src/main/res/drawable-nodpi/` and `mipmap-*/` — logo drawables
  and launcher icons.

## Rules the UIs follow

- **Dark first.** Void ground, Hull panels, 1px Line hairlines. No shadows.
- **Signal is scarce.** Phosphor only marks what is live (lights on, the
  running mode, a BLE link) or the one primary action.
- **Leave a trail.** The dotted phosphor-to-ion trail appears once per view —
  on both screens it is the brightness slider, ending in the ion signal dot.
- **Type.** Space Grotesk for words, Space Mono (UPPERCASE, tracked) for
  labels and readings. Never retype the wordmark in a font.
- **Logos** sit on dark grounds only, unrecoloured, with a quarter of the
  mark's height as clear space.
- **Status** is never colour alone: tags pair a word with a pip shape — disc
  (online), ring (scanning), diamond (warn), square (alert).

Fonts are licensed under the SIL Open Font License 1.1; see the `*-OFL.txt`
files next to them.

// alientrails
