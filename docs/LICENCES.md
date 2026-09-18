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

PSP is the only platform whose "firmware" ships with the emulator rather than
coming from RomM. Every other system's BIOS is fetched at runtime from the
person's own server and none is redistributed.

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
