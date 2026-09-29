#!/usr/bin/env python3
"""The one Xbox EEPROM every CabinetOS console uses.

WHY ONE, AND WHY IT NEVER CHANGES. Some Xbox games lock their saves to the
hard-drive key held in the EEPROM. xemu makes a random EEPROM the first time it
runs, so two consoles, or one console reinstalled, would each have their own
key and those saves would not load on the other. So every console runs this
same file, it is published, and it must never be regenerated: a new key would
strand every locked save made with the old one. Someone leaving CabinetOS
points their own xemu at it (Settings > System > EEPROM) and every save loads.
MMagTech, 2026-09-29; issue #172.

HOW IT IS MADE: exactly as xemu's own xbox_eeprom_generate (hw/xbox/
eeprom_generation.c, v0.8.136) makes one, with the version 1.0 keys it uses,
except that the video flags are set: 480p, 720p, 1080i and widescreen, so a
game that has an HD or 16:9 mode uses it (nxdk hal/video.h for the bits;
xemu-dashboard's menu_eeprom.c edits the same field). Region North America,
NTSC-M, English, as xemu's defaults.

It was run ONCE, on 2026-09-29, to make
system_files/usr/share/cabinetos/xemu/eeprom.bin. It is kept to show how that
file was made, not to be run again. `--check FILE` reads one back and says
whether it decrypts and what it holds.
"""

import os
import struct
import sys


def rol(x, n):
    return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF


def compress(h, block):
    w = list(struct.unpack(">16I", block))
    for i in range(16, 80):
        w.append(rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1))
    a, b, c, d, e = h
    for i in range(80):
        if i < 20:
            f, k = (b & c) | (~b & d), 0x5A827999
        elif i < 40:
            f, k = b ^ c ^ d, 0x6ED9EBA1
        elif i < 60:
            f, k = (b & c) | (b & d) | (c & d), 0x8F1BBCDC
        else:
            f, k = b ^ c ^ d, 0xCA62C1D6
        a, b, c, d, e = (rol(a, 5) + (f & 0xFFFFFFFF) + e + k + w[i]) & 0xFFFFFFFF, a, rol(b, 30), c, d
    return [(x + y) & 0xFFFFFFFF for x, y in zip(h, [a, b, c, d, e])]


def sha1_from(state, data):
    """SHA-1 continued from `state` after one 64-byte block (xemu's sha1_fill
    with length 512): the inner and outer halves of an HMAC with the key
    already folded in."""
    h = list(state)
    msg = data + b"\x80"
    while len(msg) % 64 != 56:
        msg += b"\x00"
    msg += struct.pack(">Q", 512 + len(data) * 8)
    for i in range(0, len(msg), 64):
        h = compress(h, msg[i:i + 64])
    return struct.pack(">5I", *h)


# XBOX_EEPROM_VERSION_R1, the version xemu generates (xbox_sha1_reset).
FIRST = (0x72127625, 0x336472B9, 0xBE609BEA, 0xF55E226B, 0x99958DAC)
SECOND = (0x76441D41, 0x4DE82659, 0x2E8EF85E, 0xB256FACA, 0xC4FE2DE8)


def xbox_hash(data):
    return sha1_from(SECOND, sha1_from(FIRST, data))


def rc4(key, data):
    s = list(range(256))
    j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) % 256
        s[i], s[j] = s[j], s[i]
    out = bytearray()
    i = j = 0
    for byte in data:
        i = (i + 1) % 256
        j = (j + s[i]) % 256
        s[i], s[j] = s[j], s[i]
        out.append(byte ^ s[(s[i] + s[j]) % 256])
    return bytes(out)


def crc(data):
    high = low = 0
    for (val,) in struct.iter_unpack("<I", data):
        total = (high << 32) | low
        high = ((total + val) >> 32) & 0xFFFFFFFF
        low = (low + val) & 0xFFFFFFFF
    return (~(high + low)) & 0xFFFFFFFF


VIDEO_WIDESCREEN = 0x00010000
VIDEO_720P = 0x00020000
VIDEO_1080I = 0x00040000
VIDEO_480P = 0x00080000


def make():
    confounder = os.urandom(8)
    hdd_key = os.urandom(16)
    region = struct.pack("<I", 1)                       # North America
    serial = bytes(b"0"[0] + (x % 10) for x in os.urandom(12))
    mac = b"\x00\x50\xF2" + os.urandom(3)
    online_key = os.urandom(16)
    video_standard = struct.pack("<I", 0x00400100)      # NTSC-M
    factory = serial + mac + b"\x00\x00" + online_key + video_standard + b"\x00" * 4
    assert len(factory) == 0x2C

    user = bytearray(156)
    user[0:44] = (b"\x00\x00\x00\x00\x47\x4D\x54\x00\x42\x53\x54\x00"
                  b"\x00\x00\x00\x00\x00\x00\x00\x00\x0A\x05\x00\x02\x03\x05\x00\x01\x00\x00"
                  b"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xC4\xFF\xFF\xFF")
    struct.pack_into("<I", user, 0x2C, 1)               # English
    struct.pack_into("<I", user, 0x30,
                     VIDEO_480P | VIDEO_720P | VIDEO_1080I | VIDEO_WIDESCREEN)

    secret = confounder + hdd_key + region
    digest = xbox_hash(secret)
    seed = xbox_hash(digest)
    out = (digest + rc4(seed, secret) + struct.pack("<I", crc(factory)) + factory +
           struct.pack("<I", crc(bytes(user[:0x5C]))) + bytes(user))
    assert len(out) == 256
    return out


def check(path):
    d = open(path, "rb").read()
    assert len(d) == 256, "an EEPROM is 256 bytes"
    digest = d[0:20]
    secret = rc4(xbox_hash(digest), d[20:48])
    ok = xbox_hash(secret) == digest
    print("decrypts:       ", "yes" if ok else "NO")
    print("region:         ", struct.unpack("<I", secret[24:28])[0])
    print("factory crc ok: ", crc(d[0x34:0x60]) == struct.unpack("<I", d[0x30:0x34])[0])
    print("user crc ok:    ", crc(d[0x64:0x64 + 0x5C]) == struct.unpack("<I", d[0x60:0x64])[0])
    print("video standard: ", hex(struct.unpack("<I", d[0x58:0x5C])[0]))
    print("language:       ", struct.unpack("<I", d[0x64 + 0x2C:0x64 + 0x30])[0])
    print("video flags:    ", hex(struct.unpack("<I", d[0x64 + 0x30:0x64 + 0x34])[0]))
    return ok


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        sys.exit(0 if check(sys.argv[2]) else 1)
    if len(sys.argv) == 3 and sys.argv[1] == "--make":
        if os.path.exists(sys.argv[2]):
            sys.exit("refusing to replace " + sys.argv[2] + ": the EEPROM must never change")
        open(sys.argv[2], "wb").write(make())
        sys.exit(0 if check(sys.argv[2]) else 1)
    sys.exit("usage: xbox-eeprom.py --check FILE | --make NEW_FILE")
