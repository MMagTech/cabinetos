#!/usr/bin/env python3
"""Copy RetroArch's GLSL shader presets into frontend/data/shaders (#122).

Every preset the console offers, and every file those presets name (pass
sources and lookup textures), fetched unchanged from libretro/glsl-shaders at
one commit, with upstream's paths kept so each preset's relative paths still
resolve. Run from the repository root; needs `gh`.

    tools/fetch-shaders.py                 the presets below, at COMMIT
    tools/fetch-shaders.py crt/foo         one more preset, to try it

The list of what the console OFFERS is frontend/src/screenfx.cpp; this list
is what it ships, and the two must agree.
"""

import os
import re
import subprocess
import sys

COMMIT = "435612fe4f1023117b3aae48c88603fb413404a3"
DEST = "frontend/data/shaders"

PRESETS = [
    "interpolation/sharp-bilinear-simple",
    "crt/crt-easymode",
    "crt/crt-easymode-halation",
    "crt/crt-lottes",
    "crt/crt-geom",
    "crt/zfast-crt",
    "crt/crt-aperture",
    "crt/crt-guest-dr-venom",
    "handheld/lcd3x",
    "handheld/lcd-grid-v2",
    "handheld/zfast-lcd",
    "handheld/gameboy",
    "handheld/gameboy-pocket",
    "handheld/gameboy-light",
    "handheld/gbc-dot-matrix-white",
]


def fetch(path):
    out = os.path.join(DEST, path)
    if os.path.exists(out):
        return open(out, "rb").read()
    data = subprocess.run(
        ["gh", "api", f"repos/libretro/glsl-shaders/contents/{path}?ref={COMMIT}",
         "-H", "Accept: application/vnd.github.raw"],
        check=True, capture_output=True).stdout
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "wb").write(data)
    print("fetched", path)
    return data


def preset(name):
    path = name + ".glslp"
    text = fetch(path).decode()
    base = os.path.dirname(path)
    kv = {}
    for line in text.splitlines():
        m = re.match(r'\s*([A-Za-z0-9_]+)\s*=\s*"?([^"]*)"?\s*$', line)
        if m:
            kv[m.group(1)] = m.group(2).strip()
    files = [v for k, v in kv.items() if re.fullmatch(r"shader\d+", k)]
    for tex in filter(None, kv.get("textures", "").split(";")):
        files.append(kv[tex.strip()])
    for f in files:
        src = os.path.normpath(os.path.join(base, f))
        body = fetch(src)
        # A pass may #include a file beside it; none of the presets above
        # does today, and this says so if one ever starts to.
        live = [l for l in body.splitlines() if l.lstrip().startswith(b"#include")]
        if src.endswith(".glsl") and live:
            print("WARNING:", src, "uses #include, which the console does not read")


for p in sys.argv[1:] or PRESETS:
    preset(p)
