#!/usr/bin/env python3
"""Regenerates every derived Alien Trails Labs brand asset from brand/.

    pip install fonttools brotli pillow
    npm i -g lv_font_conv@1.5.2
    python3 tools/brand/generate.py

Inputs (masters, copied from the Alien Trails Labs design system):
    brand/logos/at-mark.png          primary mark, transparent
    brand/logos/at-wordmark.png      ALIEN TRAILS / LABS wordmark, transparent
    brand/logos/at-mark-avatar.png   mark on void, 1024x1024 (app icon source)
    brand/fonts/*.woff2              Space Grotesk (variable) and Space Mono

Outputs:
    src/brand/at_fonts_*.c           LVGL bitmap fonts for the CYD
    src/brand/at_images.c            LVGL RGB565 logo images for the CYD
    android/app/src/main/res/...     fonts, logo drawables, launcher icons

Logos are raster masters and must never be redrawn or recoloured, so the CYD
images are straight downsamples, pre-composited onto the ground colour they
will sit on (void for the splash, hull for the top bar).
"""

import io
import os
import shutil
import subprocess
import sys
import tempfile

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BRAND = os.path.join(ROOT, "brand")
LV_OUT = os.path.join(ROOT, "src", "brand")
RES = os.path.join(ROOT, "android", "app", "src", "main", "res")

# Token values from the design system's Void theme.
VOID = (0x05, 0x0A, 0x12)
HULL = (0x0C, 0x15, 0x20)

# Printable ASCII plus the few typographic marks the UI uses: middle dot
# (reading separator), em dash, degree.
GLYPHS = "0x20-0x7E,0xB7,0x2014,0xB0"


# ---------------------------------------------------------------------------
# Fonts
# ---------------------------------------------------------------------------


def static_fonts(tmp):
    """Returns {name: path-to-ttf} for every weight we ship."""
    out = {}
    grotesk = os.path.join(BRAND, "fonts", "SpaceGrotesk-Variable.woff2")
    for name, wght in (("regular", 400), ("medium", 500), ("semibold", 600), ("bold", 700)):
        font = TTFont(grotesk)
        inst = instancer.instantiateVariableFont(font, {"wght": wght})
        inst.flavor = None
        path = os.path.join(tmp, f"space_grotesk_{name}.ttf")
        inst.save(path)
        out[f"space_grotesk_{name}"] = path
    for name in ("Regular", "Bold"):
        font = TTFont(os.path.join(BRAND, "fonts", f"SpaceMono-{name}.woff2"))
        font.flavor = None
        path = os.path.join(tmp, f"space_mono_{name.lower()}.ttf")
        font.save(path)
        out[f"space_mono_{name.lower()}"] = path
    return out


# (lvgl symbol, ttf key, px size). Names follow the token roles in the style
# guide: sans = Space Grotesk for anything read as words, mono = Space Mono
# for labels and readings.
LV_FONTS = [
    ("at_sans_20", "space_grotesk_semibold", 20),  # top-bar mode title
    ("at_sans_14", "space_grotesk_semibold", 14),  # mode buttons, button labels
    ("at_sans_13", "space_grotesk_regular", 13),   # system-row labels
    ("at_mono_11", "space_mono_bold", 11),         # LABEL eyebrows, tabs, tags
    ("at_mono_13", "space_mono_regular", 13),      # data: %, hex, readings
]


def build_lvgl_fonts(ttfs):
    exe = shutil.which("lv_font_conv")
    if exe is None:
        sys.exit("lv_font_conv not found: npm i -g lv_font_conv@1.5.2")
    for sym, key, size in LV_FONTS:
        dst = os.path.join(LV_OUT, f"{sym}.c")
        subprocess.run(
            [exe, "--font", ttfs[key], "-r", GLYPHS, "--size", str(size), "--bpp", "4",
             "--format", "lvgl", "--no-compress",
             "--lv-include", "lvgl.h", "-o", dst],
            check=True,
        )
        # The converter records its (temporary) input path; keep output stable.
        with open(dst) as f:
            text = f.read().replace(os.path.dirname(ttfs[key]), "brand/fonts").replace(ROOT + "/", "")
        with open(dst, "w") as f:
            f.write(text)
        print("wrote", os.path.relpath(dst, ROOT))


def install_android_fonts(ttfs):
    d = os.path.join(RES, "font")
    os.makedirs(d, exist_ok=True)
    for key, path in ttfs.items():
        shutil.copyfile(path, os.path.join(d, f"{key}.ttf"))
        print("wrote", os.path.relpath(os.path.join(d, f"{key}.ttf"), ROOT))


# ---------------------------------------------------------------------------
# Images
# ---------------------------------------------------------------------------


def trimmed(name, pad_frac=0.0):
    im = Image.open(os.path.join(BRAND, "logos", name)).convert("RGBA")
    box = im.split()[-1].getbbox()
    im = im.crop(box)
    if pad_frac:
        pad = int(round(max(im.size) * pad_frac))
        canvas = Image.new("RGBA", (im.width + 2 * pad, im.height + 2 * pad), (0, 0, 0, 0))
        canvas.paste(im, (pad, pad))
        im = canvas
    return im


def fit(im, w=None, h=None):
    if w is None:
        w = round(im.width * h / im.height)
    if h is None:
        h = round(im.height * w / im.width)
    return im.resize((w, h), Image.LANCZOS)


def on_ground(im, rgb):
    ground = Image.new("RGBA", im.size, rgb + (255,))
    ground.alpha_composite(im)
    return ground.convert("RGB")


def rgb565_c_array(sym, im):
    """LV_IMG_CF_TRUE_COLOR for LV_COLOR_DEPTH 16, LV_COLOR_16_SWAP 0."""
    px = []
    raw = im.tobytes()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        px += [v & 0xFF, v >> 8]
    rows = []
    for i in range(0, len(px), 24):
        rows.append("  " + ", ".join(f"0x{x:02x}" for x in px[i:i + 24]) + ",")
    body = "\n".join(rows)
    return f"""
static const uint8_t {sym}_map[] = {{
{body}
}};

const lv_img_dsc_t {sym} = {{
    .header = {{.cf = LV_IMG_CF_TRUE_COLOR, .always_zero = 0, .reserved = 0,
               .w = {im.width}, .h = {im.height}}},
    .data_size = {len(px)},
    .data = {sym}_map,
}};
"""


def build_lvgl_images():
    mark = trimmed("at-mark.png")
    wordmark = trimmed("at-wordmark.png")
    images = [
        # Splash: mark 100px tall, wordmark 256px wide, both on void.
        ("at_img_mark_splash", on_ground(fit(mark, h=100), VOID)),
        ("at_img_wordmark_splash", on_ground(fit(wordmark, w=256), VOID)),
        # Top bar stamp: 24px tall is the style guide's minimum mark size.
        ("at_img_mark_bar", on_ground(fit(mark, h=24), HULL)),
    ]
    src = [
        "// Generated by tools/brand/generate.py from brand/logos. Do not edit.",
        "// Alien Trails Labs logo rasters, RGB565, pre-composited on their ground.",
        "",
        "#include <lvgl.h>",
    ]
    for sym, im in images:
        src.append(rgb565_c_array(sym, im))
        print(f"  {sym}: {im.width}x{im.height}")
    path = os.path.join(LV_OUT, "at_images.c")
    with open(path, "w") as f:
        f.write("\n".join(src))
    print("wrote", os.path.relpath(path, ROOT))


def save_png(im, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, optimize=True)
    print("wrote", os.path.relpath(path, ROOT))


def build_android_images():
    nodpi = os.path.join(RES, "drawable-nodpi")
    save_png(fit(trimmed("at-mark.png"), h=480), os.path.join(nodpi, "at_mark.png"))
    save_png(fit(trimmed("at-wordmark.png"), w=1080), os.path.join(nodpi, "at_wordmark.png"))

    # Legacy launcher icons straight from the avatar master (mark on void).
    avatar = Image.open(os.path.join(BRAND, "logos", "at-mark-avatar.png")).convert("RGB")
    for density, px in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144),
                        ("xxxhdpi", 192)):
        save_png(avatar.resize((px, px), Image.LANCZOS),
                 os.path.join(RES, f"mipmap-{density}", "ic_launcher.png"))

    # Adaptive icon foreground: 108dp canvas, the mark held inside the 66dp
    # safe zone with its quarter-height clear space on every side.
    canvas = Image.new("RGBA", (432, 432), (0, 0, 0, 0))
    mark = fit(trimmed("at-mark.png"), h=200)
    canvas.alpha_composite(mark, ((432 - mark.width) // 2, (432 - mark.height) // 2))
    save_png(canvas, os.path.join(RES, "drawable-nodpi", "ic_launcher_foreground.png"))


def main():
    os.makedirs(LV_OUT, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        ttfs = static_fonts(tmp)
        build_lvgl_fonts(ttfs)
        install_android_fonts(ttfs)
    build_lvgl_images()
    build_android_images()


if __name__ == "__main__":
    main()
