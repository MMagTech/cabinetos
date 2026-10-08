#!/usr/bin/env python3
"""The CabinetOS tile Moonlight shows for Remote Play (issue #286).

600x800, portrait like a game case, which is the size Sunshine's own tiles
are. The cabinet from the boot logo (system_files/usr/share/plymouth/themes/
spinner/watermark.png), redrawn at size because the logo is 43 pixels tall,
over the console's purple (frontend/src/ui.h, kBackdrop*), with the name in
the console's own font.

    python3 tools/make-remoteplay-tile.py OUT.png [FONT.ttf]

Needs Pillow. Drawn four times over and shrunk, for smooth edges.
"""
import sys

from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
FONT = sys.argv[2] if len(sys.argv) > 2 else "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf"
S = 4                      # drawn at four times, then shrunk
W, H = 600 * S, 800 * S


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vgrad(draw, box, stops):
    """A vertical gradient through (position 0..1, colour) stops."""
    x0, y0, x1, y1 = box
    for y in range(y0, y1):
        t = (y - y0) / max(1, y1 - y0 - 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                draw.line([(x0, y), (x1 - 1, y)], fill=mix(c0, c1, (t - p0) / (p1 - p0)))
                break


img = Image.new("RGB", (W, H))
d = ImageDraw.Draw(img)

# The console's purple: top, a near-black middle at 0.55, darker at the foot.
vgrad(d, (0, 0, W, H), [(0.0, (0x3A, 0x22, 0x68)), (0.55, (0x12, 0x0C, 0x26)),
                        (1.0, (0x09, 0x06, 0x14))])

# THE CABINET, in the logo's proportions: a cream body with rounded corners,
# a marquee, the screen, a control panel and a foot.
cream, shade = (0xF4, 0xEE, 0xDF), (0xD9, 0xD1, 0xBE)
bw, bh = 250 * S, 330 * S
bx, by = (W - bw) // 2, 150 * S
r = 22 * S
d.rounded_rectangle((bx, by, bx + bw, by + bh), r, fill=cream)
inset = 18 * S
sx0, sx1 = bx + inset, bx + bw - inset
# Marquee: pink to peach, left to right.
my0, my1 = by + inset, by + inset + 34 * S
for x in range(sx0, sx1):
    t = (x - sx0) / (sx1 - sx0)
    d.line([(x, my0), (x, my1)], fill=mix((0xF5, 0x86, 0xB4), (0xFB, 0xC1, 0x7A), t))
# Screen: the sunset, warm yellow to red-orange, top to bottom.
sy0, sy1 = my1 + 12 * S, by + 222 * S
vgrad(d, (sx0, sy0, sx1, sy1), [(0.0, (0xFF, 0xD3, 0x6E)), (0.55, (0xFB, 0x9A, 0x4C)),
                                 (1.0, (0xEE, 0x5A, 0x36))])
# Control panel: a grey strip across, the full width of the body.
py0, py1 = sy1 + 18 * S, sy1 + 66 * S
d.rounded_rectangle((bx - 10 * S, py0, bx + bw + 10 * S, py1), 10 * S, fill=shade)
cy = (py0 + py1) // 2
dot = 11 * S
for cx, col in ((bx + 46 * S, (0x3A, 0x34, 0x44)),      # the joystick
                (bx + 118 * S, (0xEC, 0x3B, 0x5C)),     # a red button
                (bx + 156 * S, (0xF7, 0xB5, 0x2C))):    # a yellow one
    d.ellipse((cx - dot, cy - dot, cx + dot, cy + dot), fill=col)
d.rounded_rectangle((bx + 196 * S, cy - 9 * S, bx + 226 * S, cy + 9 * S), 5 * S,
                    fill=(0xF7, 0xE3, 0xC4))            # the start button
# The foot under the panel.
fy1 = by + bh
d.rounded_rectangle((bx + 8 * S, py1 + 4 * S, bx + bw - 8 * S, fy1), r, fill=cream)

# The name.
font = ImageFont.truetype(FONT, 78 * S)
name = "CabinetOS"
tw = d.textlength(name, font=font)
d.text(((W - tw) / 2, 560 * S), name, font=font, fill=(0xFF, 0xFF, 0xFF))

img.resize((W // S, H // S), Image.LANCZOS).save(OUT, optimize=True)
print("wrote", OUT)
