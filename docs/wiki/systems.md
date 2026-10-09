# Systems, formats and BIOS

CabinetOS plays **33 systems**, from the Atari 2600 to the Xbox 360, Switch,
PS3 and Wii U. Everything comes from your RomM server: games, BIOS, firmware
and keys. CabinetOS ships none of them.

## How the console finds your games

- **One Library tile per RomM platform.** The console matches RomM's platform
  slug (the left column below). The tile's name is RomM's own name for the
  platform.
- **A platform the console cannot play gets no tile.** Its games still show in
  Search and in collections.
- **A tile whose emulator is still being set up is greyed**, with *Emulator not
  installed*. Eden (Switch) and xemu (Xbox) come from Flathub on the first boot
  with a network, so they can take a few minutes to appear.

## How BIOS, firmware and keys reach the console

Put them in RomM as **firmware for that platform** (in RomM's library, the
`bios/<platform>/` folder; see [RomM's documentation](https://docs.romm.app)).
Then do nothing: every launch asks RomM for that platform's firmware and
downloads anything new into the console's shared BIOS folder.

- Files already on the console at the same size are not fetched again.
- For the systems marked **any name** below, the console recognises the file by
  its size or contents and copies it under the name the emulator expects. The
  names given are the usual ones.
- Offline, the console uses what it already has.
- When a required BIOS is missing, the game does not start and the screen says
  so, for example *No Sega Saturn BIOS on your server*. For Switch, PS3 and
  Xbox it says *keys*, *firmware* or *BIOS*.

## File formats in short

- **Single files play as they are**, whatever their extension: `.chd`, `.rvz`,
  `.iso`, `.sfc` and so on.
- **Zip, 7z, rar and gz are unpacked** before the game starts, except for
  arcade, Dreamcast, GameCube, Wii and Saturn, which are handed the archive
  unopened. **Do not zip Dreamcast, GameCube, Wii or Saturn discs**: use CHD
  for Dreamcast and Saturn, RVZ or ISO for GameCube and Wii.
- **CD systems: prefer CHD.** A zipped cue/bin set works when the `.cue` comes
  first in the archive.
- **Multi-disc games play their first disc.** Changing discs in a game is not
  built yet.

## Every system

| System | RomM slug | Emulator | Formats | BIOS, firmware or keys |
|---|---|---|---|---|
| 3DO | `3do` | Opera | iso, bin, chd, cue | **Required:** `panafz10.bin` (1 MB, any name) |
| Arcade | `arcade` | FinalBurn Neo or MAME 2003-Plus | zip (also 7z for FinalBurn Neo) | None. See [Arcade](#arcade) |
| Atari 2600 | `atari2600` | Stella 2014 | a26, bin | None |
| Atari 7800 | `atari7800` | ProSystem | a78, bin | None |
| Dreamcast | `dc` | Flycast | chd, cdi, gdi, cue, m3u | Optional: `dc_boot.bin` (2 MB, any name). Without it Flycast uses its own. |
| Game Boy | `gb` | Gambatte | gb, dmg | None |
| Game Boy Color | `gbc` | Gambatte | gbc | None |
| Game Boy Advance | `gba` | mGBA | gba | Optional: `gba_bios.bin` |
| Game Gear | `gamegear` | Genesis Plus GX | gg | None |
| Genesis / Mega Drive | `genesis` | Genesis Plus GX | md, gen, smd, bin | None |
| Master System | `sms` | Genesis Plus GX | sms | None |
| Sega 32X | `sega32` | PicoDrive | 32x, bin | None |
| Sega CD | `segacd` | Genesis Plus GX | chd, cue, iso | **Required:** `bios_CD_U.bin`, `bios_CD_E.bin` or `bios_CD_J.bin` (128 KB, any name) |
| Saturn | `saturn` | Beetle Saturn | chd, cue, ccd, toc, m3u | **Required:** `sega_101.bin` (Japan) or `mpr-17933.bin` (US and Europe) (512 KB, any name) |
| NES | `nes` | FCEUmm | nes, fds, unf, unif | None |
| SNES | `snes` | Snes9x | sfc, smc, swc, fig, bs, st | None |
| Nintendo 64 | `n64` | Mupen64Plus-Next | z64, n64, v64 | None |
| Nintendo DS | `nds` | melonDS | nds, dsi, ids | Optional: `bios7.bin`, `bios9.bin`, `firmware.bin` |
| GameCube | `ngc` | Dolphin | rvz, iso, gcz, ciso, wia, gcm | None |
| Wii | `wii` | Dolphin | rvz, iso, wbfs, wia, ciso, wad | None. See [Wii](#wii-and-wii-u) |
| Wii U | `wiiu` | Cemu | wua | None (a `.wua` is already decrypted) |
| Switch | `switch` | Eden | nsp, xci | **Required:** `prod.keys`. Optional: `title.keys`, and a firmware zip (any zip with *firmware* in its name) |
| Neo Geo Pocket Color | `neo-geo-pocket-color` | Beetle NGP | ngp, ngc, ngpc | None |
| PlayStation | `psx` | PCSX ReARMed | chd, cue, bin, pbp, m3u, img, iso | Optional: `scph5501.bin` (or `scph5500`, `scph5502`, `scph1001`, `scph7001`, `scph101`). Without one it uses its own. |
| PlayStation 2 | `ps2` | PCSX2 | chd, iso | **Required:** a PS2 BIOS dump, for example `SCPH-70012.bin` (any name) |
| PSP | `psp` | PPSSPP | iso, cso, chd, pbp | None |
| PlayStation 3 | `ps3` | RPCS3 | decrypted iso, or pkg | **Required:** `PS3UPDAT.PUP` (installed once). A PKG game also needs its `.rap` licence in the same RomM game folder. |
| TurboGrafx-16 | `tg16` | Beetle PCE Fast | pce | None |
| TurboGrafx-CD | `turbografx-cd` | Beetle PCE Fast | chd, cue, ccd | **Required:** `syscard3.pce` (256 KB, any name) |
| Vectrex | `vectrex` | vecx | vec, bin | None |
| Virtual Boy | `virtualboy` | Beetle VB | vb, vboy | None |
| Xbox | `xbox` | xemu | iso, xiso | **Required:** the MCPX boot ROM (512 bytes, for example `mcpx_1.0.bin`) and a flash BIOS (256 KB, 512 KB or 1 MB), recognised by contents, any names |
| Xbox 360 | `xbox360` | Xenia | iso, xex, zar, LIVE and CON packages | None |

## Arcade

- RomM's arcade platform is split by folder. The console plays an arcade
  folder named exactly **`FBNEO`** with FinalBurn Neo, and one named exactly
  **`MAME2003`** with MAME 2003-Plus (names are case sensitive). Map both to
  RomM's arcade platform. Any other arcade folder name gets no tile.
- **FinalBurn Neo:** non-merged sets for FinalBurn Neo 1.0.0.03, with any BIOS
  inside each game's zip.
- **MAME 2003-Plus:** MAME 0.78 sets.
- The zip must keep its set name, for example `sf2.zip`.

## Wii and Wii U

- A Wii game that takes a **Classic Controller** gets one on every pad. One that
  takes a **GameCube pad** gets that. Anything else needs a real **Wii Remote**
  (see [Controllers](controllers.md)) and is greyed with *Needs a Wii Remote*
  until one is paired.
- Your Miis from the Mii Channel are copied into every Wii game.
- Wii U games that need a Wii Remote or the GamePad are greyed: *Needs a Wii
  Remote*, *Needs a Wii U GamePad*.

## Not supported

- **Game & Watch** is left out on purpose: the games are too small to play on a
  television.
- Platforms not in the table above (for example Jaguar, ColecoVision, Vita)
  get no tile.

## What works where

| | Save states, rewind, fast forward | Screen looks | Achievements |
|---|---|---|---|
| 2600 to Saturn, N64, Dreamcast, PS1, PSP, DS (built-in emulators) | Yes | Most | Yes |
| PS2 | No | No | Yes |
| GameCube, Wii | No | No | Yes |
| Switch, PS3, Xbox, Xbox 360, Wii U | No | No | No |

Saves sync for every system. See [Saves, states and sync](saves.md).
