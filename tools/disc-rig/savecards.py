#!/usr/bin/env python3
# Put a downloaded save on the memory card the console's emulator reads (#162,
# the disc rig): so a test can start at a game's disc change without playing
# to it. Writes a new card; the input card is never changed.
#
#   savecards.py ps1  <file.gme|.mcr> <out.srm>
#       A DexDrive card (.gme, 3904-byte header) or a raw card: the 128 KB
#       card PCSX ReARMed reads as save RAM.
#   savecards.py vmu  <base-vmu.bin|-> <out.srm> <save.dci>...
#       Dreamcast DCI saves (32-byte directory entry, then the data in 4-byte
#       swapped groups) onto a VMU image: Flycast's vmu_save_A1.bin. "-" for
#       a freshly formatted card.
#   savecards.py gc   <base-card.raw> <out.raw> <save.gci>...
#       GameCube GCI saves onto a Dolphin raw card, a formatted one as base.
import struct
import sys
import time


def ps1(src, out):
    b = open(src, 'rb').read()
    if b[:11] == b'123-456-STD':
        b = b[3904:]
    if len(b) != 131072 or b[:2] != b'MC':
        sys.exit('not a PS1 memory card')
    open(out, 'wb').write(b)
    names = [b[0x80 * i + 0x0a:0x80 * i + 0x1e].split(b'\0')[0].decode('ascii', 'replace')
             for i in range(1, 16) if b[0x80 * i] == 0x51]
    print('PS1 card:', ', '.join(names))


def bcd(n):
    return ((n // 10) << 4) | (n % 10)


def vmu_format():
    v = bytearray(131072)
    root = bytearray(512)
    root[0:16] = b'\x55' * 16
    t = time.localtime()
    root[0x30:0x38] = bytes([bcd(t.tm_year // 100), bcd(t.tm_year % 100), bcd(t.tm_mon),
                             bcd(t.tm_mday), bcd(t.tm_hour), bcd(t.tm_min), bcd(t.tm_sec),
                             t.tm_wday])
    struct.pack_into('<HHHHHH', root, 0x46, 254, 1, 253, 13, 0, 200)
    v[255 * 512:] = root
    fat = [0xFFFC] * 256
    fat[255] = fat[254] = 0xFFFA
    for b in range(253, 241, -1):
        fat[b] = b - 1
    fat[241] = 0xFFFA
    v[254 * 512:255 * 512] = struct.pack('<256H', *fat)
    return v


def vmu(base, out, dcis):
    v = vmu_format() if base == '-' else bytearray(open(base, 'rb').read())
    if len(v) != 131072 or v[255 * 512:255 * 512 + 16] != b'\x55' * 16:
        sys.exit('base is not a formatted VMU')
    fat = list(struct.unpack('<256H', v[254 * 512:255 * 512]))
    for path in dcis:
        d = open(path, 'rb').read()
        entry = bytearray(d[:32])
        data = d[32:]
        size = struct.unpack('<H', entry[0x18:0x1a])[0]
        if len(data) != size * 512:
            sys.exit(f'{path}: {len(data)} bytes for {size} blocks')
        # DCI keeps the data in 4-byte groups, each reversed.
        data = b''.join(data[i:i + 4][::-1] for i in range(0, len(data), 4))
        name = bytes(entry[4:16])
        # One file per name: a save already there is replaced.
        for blk in range(253, 240, -1):
            for i in range(16):
                o = blk * 512 + i * 32
                if v[o] and v[o + 4:o + 16] == name:
                    b = struct.unpack('<H', v[o + 2:o + 4])[0]
                    while b != 0xFFFA:
                        nb = fat[b]
                        fat[b] = 0xFFFC
                        b = nb
                    v[o:o + 32] = bytes(32)
        # Data files take the user area from block 199 down, as the BIOS does.
        free = [b for b in range(199, -1, -1) if fat[b] == 0xFFFC][:size]
        if len(free) < size:
            sys.exit('VMU full')
        for a, b in zip(free, free[1:] + [None]):
            fat[a] = b if b is not None else 0xFFFA
        for k, b in enumerate(free):
            v[b * 512:(b + 1) * 512] = data[k * 512:(k + 1) * 512]
        struct.pack_into('<H', entry, 2, free[0])
        slot = next(blk * 512 + i * 32 for blk in range(253, 240, -1) for i in range(16)
                    if not v[blk * 512 + i * 32])
        v[slot:slot + 32] = entry
        print(f'VMU: {name.decode("ascii", "replace")}, {size} blocks from block {free[0]}')
    v[254 * 512:255 * 512] = struct.pack('<256H', *fat)
    open(out, 'wb').write(v)


def gc_checksum(data):
    c1 = c2 = 0
    for i in range(0, len(data), 2):
        w = (data[i] << 8) | data[i + 1]
        c1 = (c1 + w) & 0xFFFF
        c2 = (c2 + (w ^ 0xFFFF)) & 0xFFFF
    if c1 == 0xFFFF:
        c1 = 0
    if c2 == 0xFFFF:
        c2 = 0
    return c1, c2


def gc(base, out, gcis):
    B = 0x2000
    c = bytearray(open(base, 'rb').read())
    nblocks = len(c) // B
    # The live directory and block map are the copies with the higher counter.
    dirs = [c[B:2 * B], c[2 * B:3 * B]]
    bats = [c[3 * B:4 * B], c[4 * B:5 * B]]
    dcount = [struct.unpack('>h', d[0x1FFA:0x1FFC])[0] for d in dirs]
    bcount = [struct.unpack('>h', b[4:6])[0] for b in bats]
    d = bytearray(dirs[0 if dcount[0] >= dcount[1] else 1])
    bat = bytearray(bats[0 if bcount[0] >= bcount[1] else 1])
    nmap = nblocks - 5
    m = list(struct.unpack(f'>{nmap}H', bat[0xA:0xA + 2 * nmap]))
    for path in gcis:
        g = open(path, 'rb').read()
        entry = bytearray(g[:0x40])
        body = g[0x40:]
        count = struct.unpack('>H', entry[0x38:0x3A])[0]
        if len(body) != count * B:
            sys.exit(f'{path}: {len(body)} bytes for {count} blocks')
        key = bytes(entry[0:6]) + bytes(entry[8:0x28])
        for i in range(127):
            o = i * 0x40
            if d[o] != 0xFF and bytes(d[o:o + 6]) + bytes(d[o + 8:o + 0x28]) == key:
                b = struct.unpack('>H', d[o + 0x36:o + 0x38])[0]
                while b != 0xFFFF and b >= 5:
                    nb = m[b - 5]
                    m[b - 5] = 0
                    b = nb
                d[o:o + 0x40] = b'\xff' * 0x40
        free = [k + 5 for k in range(nmap) if m[k] == 0][:count]
        if len(free) < count:
            sys.exit('card full')
        for a, b in zip(free, free[1:] + [None]):
            m[a - 5] = b if b is not None else 0xFFFF
        for k, b in enumerate(free):
            c[b * B:(b + 1) * B] = body[k * B:(k + 1) * B]
        struct.pack_into('>H', entry, 0x36, free[0])
        slot = next(i for i in range(127) if d[i * 0x40] == 0xFF)
        d[slot * 0x40:(slot + 1) * 0x40] = entry
        fname = entry[8:0x28].split(b'\0')[0].decode('ascii', 'replace')
        print(f'GC: {entry[0:6].decode()} {fname}, {count} blocks from block {free[0]}')
    counter = max(dcount + bcount) + 1
    struct.pack_into('>h', d, 0x1FFA, counter)
    struct.pack_into('>HH', d, 0x1FFC, *gc_checksum(d[:0x1FFC]))
    struct.pack_into(f'>{nmap}H', bat, 0xA, *m)
    struct.pack_into('>h', bat, 4, counter)
    struct.pack_into('>H', bat, 6, sum(1 for x in m if x == 0))
    used = [k + 5 for k in range(nmap) if m[k]]
    struct.pack_into('>H', bat, 8, max(used) if used else 4)
    struct.pack_into('>HH', bat, 0, *gc_checksum(bat[4:B]))
    # Both copies the same, so neither can win with stale contents.
    c[B:2 * B] = d
    c[2 * B:3 * B] = d
    c[3 * B:4 * B] = bat
    c[4 * B:5 * B] = bat
    open(out, 'wb').write(c)


if __name__ == '__main__':
    kind = sys.argv[1]
    if kind == 'ps1':
        ps1(sys.argv[2], sys.argv[3])
    elif kind == 'vmu':
        vmu(sys.argv[2], sys.argv[3], sys.argv[4:])
    elif kind == 'gc':
        gc(sys.argv[2], sys.argv[3], sys.argv[4:])
    else:
        sys.exit(__doc__)
