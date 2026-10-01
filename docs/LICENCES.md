# Licences

CabinetOS's own source is MIT. **Almost nothing else in the image is**, and this
page lists what is in there, where it came from, the exact commit it was built
from, and under what terms.

Modelled on Cabinet's `docs/licenses.md`, which had already solved this. The
commits below are **CabinetOS's**, not Cabinet's — eleven cores are pinned to a
different revision than Cabinet's iOS build ships, and the pins here are the
ones `cores/build-core.sh` asserts at build time.

**CabinetOS ships no games and no BIOS files.** Both come from the person's own
RomM server at runtime and neither is redistributed here.

---

## The constraint this project is under, said plainly

**Six of the twenty-one cores are free for non-commercial use only.** Not GPL —
a different thing, and a stricter one. They cover Arcade, SNES, Genesis, Sega
CD, Master System, Game Gear, 32X and 3DO, which is a large slice of the
library.

> **CabinetOS is free, is not sold, and takes no donations. That is what keeps
> those cores legitimate in this build, and it is a deliberate constraint rather
> than an oversight.** Selling a machine with this image on it, charging for the
> image, or adding a sponsor button would break it.

Cabinet wrote the same paragraph about itself and it is worth repeating here
because **the risk is sharper for an operating system than for an app**: an OS
is something you can plausibly put on a box and sell, and this project has
already discussed changing hardware. The day money changes hands for a machine
carrying this image, six cores have to come out or be relicensed.

## GPL, and what it actually requires

Most of the rest is GPL, and GPL is not a restriction on use. Anyone may run it,
study it, modify it privately, and redistribute it — including commercially. The
obligation lands only on **distribution**: pass on the same freedoms, and make
the corresponding source available.

**The cores are `dlopen`ed rather than statically linked**, which is a narrower
coupling than Cabinet's build has, but the conservative reading is the same one
Cabinet states about itself: the distributed image is a combined work under
those terms. The corresponding source for every GPL component is the upstream
commit named below, unmodified except where `cores/build-core.sh` says
otherwise, and that script is in this repository so anyone can reproduce the
binaries exactly. Two cores have been shown to build byte-identically on two
different machines.

**Source patches are part of this.** `cores/build-core.sh` applies three
in-flight patches — melonDS's unload flush, and PPSSPP's shader-cache save and
CPU-engine log. They are visible in that script, they are applied at build time
rather than vendored, and they are the modifications this project makes to GPL
source.

---

## CabinetOS

| | |
|---|---|
| Licence | MIT |
| Source | This repository |

Covers `frontend/`, `cores/`, `build_files/`, `ci/` and `tools/`. Our
`Justfile`, `.github/workflows/` and `disk_config/` derive from
[ublue-os/image-template](https://github.com/ublue-os/image-template), which is
Apache-2.0.

## The base image

CabinetOS is built on [Bazzite](https://github.com/ublue-os/bazzite), itself
built on Fedora. Every package in the image keeps the licence it ships under
from those projects, and their terms are satisfied by that provenance and by the
credit given here and in the README. The base is pinned by digest in
`Containerfile`.

## Emulator cores

Each is built from upstream at the commit below by `cores/build-core.sh`, which
checks out that exact revision and asserts it, then reads the revision back out
of the finished `.so` and fails the build if it does not match.

| Core | Systems | Licence | Upstream | Commit |
|---|---|---|---|---|
| FinalBurn Neo | Arcade | **Non-commercial**, plus MAME's terms | [libretro/FBNeo](https://github.com/libretro/FBNeo) | `2444fbe3ddab` |
| MAME 2003-Plus | Arcade | **MAME 0.78 non-commercial** | [libretro/mame2003-plus-libretro](https://github.com/libretro/mame2003-plus-libretro) | `21256d24120b` |
| Snes9x | SNES | **Non-commercial** | [libretro/snes9x](https://github.com/libretro/snes9x) | `890b5d445538` |
| Genesis Plus GX | Genesis, Sega CD, Master System, Game Gear | **Non-commercial** | [libretro/Genesis-Plus-GX](https://github.com/libretro/Genesis-Plus-GX) | `a7985a9c4278` |
| PicoDrive | Sega 32X | **Non-commercial** | [libretro/picodrive](https://github.com/libretro/picodrive) | `733c711a477a` |
| Opera | 3DO | **Modified LGPL, non-commercial** (FreeDO terms) | [libretro/opera-libretro](https://github.com/libretro/opera-libretro) | `a501a278d057` |
| Flycast | Dreamcast, Naomi | GPL v2 | [flyinghead/flycast](https://github.com/flyinghead/flycast) | `a172e0001351` |
| PPSSPP | PSP | GPL v2 or later | [hrydgard/ppsspp](https://github.com/hrydgard/ppsspp) | `c989c2553e10` |
| Dolphin | GameCube (and Wii, from the same core) | GPL v2 or later | [libretro/dolphin](https://github.com/libretro/dolphin) | `1a0f97270b70` |
| mupen64plus-libretro-nx (bundles GLideN64) | Nintendo 64 | GPL v2 | [libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx) | `f275caf4b2bf` |
| PCSX ReARMed | PlayStation | GPL v2 | [libretro/pcsx_rearmed](https://github.com/libretro/pcsx_rearmed) | `ba61a4fdee1f` |
| Beetle Saturn | Saturn | GPL v2 | [libretro/beetle-saturn-libretro](https://github.com/libretro/beetle-saturn-libretro) | `ed549bdac0e1` |
| Beetle PCE Fast | TurboGrafx-16, TurboGrafx-CD | GPL v2 | [libretro/beetle-pce-fast-libretro](https://github.com/libretro/beetle-pce-fast-libretro) | `2f623abd0332` |
| Beetle NeoPop | Neo Geo Pocket Color | GPL v2 | [libretro/beetle-ngp-libretro](https://github.com/libretro/beetle-ngp-libretro) | `a50d5ac288a8` |
| Beetle VB | Virtual Boy | GPL v2 | [libretro/beetle-vb-libretro](https://github.com/libretro/beetle-vb-libretro) | `83ed42608601` |
| FCEUmm | NES | GPL v2 | [libretro/libretro-fceumm](https://github.com/libretro/libretro-fceumm) | `236ccdfc911e` |
| Gambatte | Game Boy, Game Boy Color | GPL v2 | [libretro/gambatte-libretro](https://github.com/libretro/gambatte-libretro) | `d9d6cd06382d` |
| ProSystem | Atari 7800 | GPL v2 | [libretro/prosystem-libretro](https://github.com/libretro/prosystem-libretro) | `8a88014287c7` |
| Stella 2014 | Atari 2600 | GPL v2 | [libretro/stella2014-libretro](https://github.com/libretro/stella2014-libretro) | `4a7da82595d2` |
| melonDS | Nintendo DS | GPL v3 | [libretro/melonDS](https://github.com/libretro/melonDS) | `66b5d2634cd0` |
| DraStic FreeBIOS | DS BIOS replacement inside melonDS | BSD 2-clause | bundled in the fork above | `66b5d2634cd0` |
| vecx | Vectrex | GPL v3 | [libretro/libretro-vecx](https://github.com/libretro/libretro-vecx) | `8f671cc9d737` |
| mGBA | Game Boy Advance | MPL 2.0 | [libretro/mgba](https://github.com/libretro/mgba) | `e31759b24e7a` |

**Not carried from Cabinet:** `gw-libretro` (Game & Watch) and `vemulator`
(Dreamcast VMU minigames) are iOS-only in Cabinet by decision and are not built
here. See `frontend/src/catalog.cpp`.

## Files shipped inside the image that are not cores

| What | Where it comes from | Licence |
|---|---|---|
| **PPSSPP system files** — fonts, VFPU tables, `compat.ini`, the atlases | PPSSPP's own `assets/`, installed by `cores/build-core.sh` | GPL v2 or later, as PPSSPP |
| **FFmpeg**, statically linked inside PPSSPP | the prebuilt `ffmpeg/linux/x86_64` in PPSSPP's own tree | LGPL v2.1 or later |
| **Noto Sans** and Noto Sans CJK, the interface type | already in the Bazzite base; nothing is bundled | SIL Open Font License 1.1 |
| **PCSX2**, linked into `cabinetos-ps2.so` | upstream `PCSX2/pcsx2` at v2.8.2, built by `cores/build-pcsx2.sh` | GPL v3 or later |
| **PCSX2's resources** — `GameIndex.yaml`, the Redump database, fonts, GS shaders | PCSX2's own `bin/resources` | GPL v3 or later, as PCSX2 |
| **rapidyaml** and **c4core**, carried beside the emulator | Fedora's packages, copied because the Bazzite base lacks them | MIT |
| **RPCS3**, in `/usr/lib/cabinetos/rpcs3` | the RPCS3 team's official Linux build `0.0.42-20076-1707d7fc` (release `build-1707d7fc…` of `RPCS3/rpcs3-binaries-linux`), unmodified, pinned by checksum in `build_files/install-rpcs3.sh`; source is `RPCS3/rpcs3` at commit `1707d7fc883ef48ff21bdcbb0141a3211ae09cb2` | GPL v2 |
| **The libraries RPCS3's build carries** (Qt 6, FFmpeg, SDL 3, OpenCV, OpenAL and others, in its `usr/lib`) | bundled by the RPCS3 team in the same build, unchanged | each under its own licence, as RPCS3 ships them (Qt LGPL v3, FFmpeg LGPL v2.1 or later, SDL zlib) |
| **xemu's blank Xbox hard drive**, `/usr/share/cabinetos/xemu/xbox_hdd.qcow2` | `xbox_hdd.qcow2` from `xemu-project/xemu-dashboard` release `v20260516-0955`, unmodified, pinned by checksum in `build_files/install-xemu-drive.sh`. It holds the open-source xemu-dashboard and nothing of Microsoft's | MIT, with the libraries the dashboard is built from (nxdk and others) under their own licences, as xemu-dashboard ships them |
| **Dolphin's `Sys` folder** — per-game settings, the Wii's `shared2` files, shaders, the cheat code handler | Dolphin's own `Data/Sys` at the core's pinned commit, installed by `cores/build-core.sh` | GPL v2 or later, as Dolphin |
| **The Xbox EEPROM**, `/usr/share/cabinetos/xemu/eeprom.bin` | made once by this project's `tools/xbox-eeprom.py`, the way xemu makes one | MIT, as CabinetOS |
| **Xenia Edge**, in `/usr/lib/cabinetos/xenia` | its developer's official Linux build, release `a7c39fa` of `has207/xenia-edge` (`xenia_edge_linux.AppImage`), unmodified, pinned by checksum in `build_files/install-xenia.sh`; source is `has207/xenia-edge` at commit `a7c39fa7d9c54d83022e431da62c1c65bf3e6196` | BSD 3-Clause |
| **The libraries Xenia Edge's build carries** (GTK 3, GLib, cairo, SDL 3, X11 libraries and others, in its `usr/lib`) | bundled by Edge's own AppImage build, unchanged | each under its own licence, as Edge ships them (GTK, GLib and cairo LGPL, SDL zlib, the X11 libraries MIT) |

PSP is the only platform whose "firmware" ships with the emulator rather than
coming from RomM. Every other system's BIOS is fetched at runtime from the
person's own server and none is redistributed.

**PLAYSTATION 2 IS NOT A LIBRETRO CORE and is listed here rather than in the
table above.** It is the whole PCSX2 emulator built as a static library, with
CabinetOS's own host layer compiled against it and the two linked into one
shared object. That makes `cabinetos-ps2.so` a combined work under the GPL v3,
and this project's own sources are offered under the same terms — see
*CabinetOS* above. **There is one patch**, three lines in
`AudioStream::CreateStream` to stop PCSX2 opening a sound device of its own; it
lives in `cores/build-pcsx2.sh` where anyone can read it, which is what GPL v3
section 5 asks of a modified version.

**PLAYSTATION 3 IS A WHOLE PROGRAM, NOT A CORE**, run beside the frontend
rather than inside it. It is the RPCS3 team's own build, shipped exactly as
they publish it, and only unpacked from its AppImage; nothing in it is patched.
**XBOX 360 IS A WHOLE PROGRAM TOO**, Xenia Edge's own build, shipped as its
developer publishes it and only unpacked; nothing is patched. The profile file
the console writes for it (`frontend/src/x360profile.cpp`) is encrypted the
way Edge encrypts its own, with the key Edge publishes in its BSD-licensed
source; no game, BIOS or firmware is involved, and Xbox 360 needs none.
Switch (Eden) and Xbox (xemu) are not in the image at all: each console
installs them from Flathub, which distributes them (open question 21). Only
xemu's blank drive and the EEPROM are in the image, in the table above; the
Xbox BIOS and boot ROM come from the person's own RomM, like every other
system's.

## Libraries the frontend links

All are present in the Bazzite base rather than vendored here, so the image
carries them under the terms Fedora ships them with.

| Library | Used for | Licence |
|---|---|---|
| SDL3 | Window, input, audio | zlib licence |
| Mesa (EGL, GLES) | The graphics context | MIT |
| FreeType | Text rasterising | FTL or GPL v2 |
| libjpeg-turbo, libpng | Cover art | BSD-style, libpng licence |
| libcurl | The RomM client | curl licence (MIT-style) |
| json-c | Parsing RomM's responses | MIT |
| libarchive | ROM archives, and PSP save zips | BSD 2-clause |
| zlib | Compression under several of the above | zlib licence |

**SDL_GameControllerDB** (github.com/mdqinc/SDL_GameControllerDB, zlib
licence) is carried as data, not linked: `gamecontrollerdb.txt` in
`/usr/share/cabinetos/`, unmodified. Pinned in this repository
(`frontend/data/`, installed with the frontend so an update ships one file) and
refreshed by a weekly pull request (`.github/workflows/pad-db-update.yml`),
which names the upstream commit; first pinned at `c6d6e7ecca57`
(2026-09-24). It is the community's list of controllers SDL does not know by
itself (issue #66).

**GameTDB's Wii database** (www.gametdb.com, community-kept since 2009) is
carried as data: `/usr/share/cabinetos/wii-controls.txt`, an extract of which
controllers each Wii game accepts, made by `tools/wii-controls.py` from the
full `wiitdb.xml` and pinned in `frontend/data/` with the database version in
its first line. GameTDB's file offers it "for anyone to use in any Wii-related
project" and asks for permission only for use on a website. It decides which
Wii games play on an ordinary pad (docs/PROJECT.md open question 35).

## Not bundled

**Games, BIOS and firmware.** All come from the person's own RomM server.

**RomM's own web player** runs emulators inside a page served by that server.
CabinetOS does not bundle them and they are RomM's attribution to make.

---

## Where this lives on a running machine

**`/usr/share/licenses/cabinetos/`**, installed by `build_files/build.sh` and
asserted there rather than assumed. The person most likely to redistribute this
image is the one who pulls it and never sees this repository, so the terms
travel with the binaries instead of sitting beside them.

## What this document still owes

- **The full licence text of each core**, readable on the console itself.
  Cabinet puts them under Settings → Licenses; `docs/PROJECT.md` already says
  credits belong in Settings → About. Neither screen exists yet.
- **Verification of each licence line above against the source tree it came
  from.** The licences are taken from Cabinet's own list, which was compiled
  from the upstream repositories — but this project's standing rule is that a
  fact carried across from another document is a fact nobody has checked here.
