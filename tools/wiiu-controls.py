#!/usr/bin/env python3
"""Which controllers every Wii U game accepts, from GameTDB, as the console reads it.

WHAT IT IS FOR. A Wii U game plays on an ordinary pad here only when it accepts
a Pro Controller or a Classic Controller; one that takes a Wii Remote and
neither needs a real Remote (#200), and every other was made for the GamePad
and is greyed for good. MMagTech, 2026-10-01: Wii's rule carried over;
docs/PROJECT.md open question 36, issue #174. Cemu carries no such list.

WHERE IT COMES FROM. GameTDB (https://www.gametdb.com), wiiutdb.zip, the full
file with no LANG; credited in docs/LICENCES.md beside the Wii list.

WHAT IT WRITES, frontend/data/wiiu-controls.txt: one line per GameTDB ID (a
disc's six characters, BWPE01; an eShop title's four, WKNE), the ID and then
one letter per controller GameTDB lists, required or optional alike:

    p Pro Controller   c Classic Controller   w Wii Remote   n Nunchuk
    g GamePad          b Balance Board        h wheel        m microphone

An entry that lists no controls at all is left out: the console treats a
missing ID as needing the GamePad, which is the same answer.

Usage: tools/wiiu-controls.py [wiiutdb.xml]   (downloads the full file without one)
"""

import io
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

LETTERS = {
    "procontroller": "p",
    "classiccontroller": "c",
    "wiimote": "w",
    "nunchuk": "n",
    "pad": "g",
    "balanceboard": "b",
    "wheel": "h",
    "microphone": "m",
}

OUT = Path(__file__).resolve().parent.parent / "frontend" / "data" / "wiiu-controls.txt"


def load(argv):
    if len(argv) > 1:
        return Path(argv[1]).read_bytes()
    req = urllib.request.Request("https://www.gametdb.com/wiiutdb.zip",
                                 headers={"User-Agent": "Mozilla/5.0 (CabinetOS tools/wiiu-controls.py)"})
    with urllib.request.urlopen(req, timeout=120) as r:
        z = zipfile.ZipFile(io.BytesIO(r.read()))
    return z.read("wiiutdb.xml")


def main(argv):
    root = ET.fromstring(load(argv))
    version = root.find("WiiUTDB").get("version")
    rows = {}
    for g in root.findall("game"):
        gid = (g.findtext("id") or "").strip()
        inp = g.find("input")
        if not gid or inp is None:
            continue
        flags = sorted({LETTERS[c.get("type")] for c in inp.findall("control")
                        if c.get("type") in LETTERS})
        if flags:
            rows[gid] = "".join(flags)
    with OUT.open("w") as f:
        f.write(f"# GameTDB wiiutdb.xml version {version} (https://www.gametdb.com).\n")
        f.write("# Made by tools/wiiu-controls.py; regenerate rather than edit.\n")
        for gid in sorted(rows):
            f.write(f"{gid} {rows[gid]}\n")
    print(f"wrote {OUT} ({len(rows)} games, GameTDB {version})")


if __name__ == "__main__":
    main(sys.argv)
