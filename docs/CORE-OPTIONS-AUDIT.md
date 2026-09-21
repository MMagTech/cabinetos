# What every core can actually be configured with

**Audited 2026-09-20 against the twenty-three cores as this console builds and
runs them**, not against upstream documentation. Every line below was read out
of the loaded `.so` by the host that drives it.

It exists because of a question MMagTech asked, which is
[open question 23](PROJECT.md):

> I hate messing with settings in emulators. What I'd want me or anyone to
> experience is something like a performance and quality setting that affects
> all cores.

**The answer turns on what these cores actually expose**, and nobody had ever
looked at all of it at once.

## How to reproduce this

```
cabinetos-frontend --core-options-detail --core-dir <dir>
```

**THREE CORES ARE INVISIBLE TO THAT COMMAND and they are three of the most
configurable.** Dolphin, FBNeo and MAME 2003-Plus declare nothing until they
know what they are running — Dolphin reports **zero** options at load and
**103** once a GameCube disc is in. For those, load a game:

```
cabinetos-frontend --core-options-detail --launch <romId> --launch-after 0
```

A core reporting no options is the suspicious case, not the clean one.

---

## Every core

| System | Core | Options | Has a resolution dial |
|---|---|---:|---|
| GameCube | `dolphin` | 103 | **yes** |
| Arcade (FinalBurn Neo) | `fbneo` | 20 | **yes** |
| Dreamcast, Naomi | `flycast` | 89 | **yes** |
| Nintendo 64 | `mupen64plus` | 83 | **yes** |
| 3DO | `opera` | 18 | **yes** |
| PlayStation 2 | `pcsx2` | 78 | **yes** |
| PlayStation | `pcsx_rearmed` | 54 | **yes** |
| PSP | `ppsspp` | 75 | **yes** |
| Neo Geo Pocket Color | `beetle_ngp` | 1 | no |
| TurboGrafx-16, TurboGrafx-CD | `beetle_pce_fast` | 32 | no |
| Saturn | `beetle_saturn` | 28 | no |
| Virtual Boy | `beetle_vb` | 7 | no |
| NES | `fceumm` | 0 | no |
| Game Boy, Game Boy Color | `gambatte` | 32 | no |
| Mega Drive, Master System, Game Gear, Sega CD | `genesis_plus_gx` | 62 | no |
| Arcade (MAME 2003-Plus) | `mame2003_plus` | 23 | no |
| Nintendo DS | `melonds` | 20 | no |
| Game Boy Advance | `mgba` | 17 | no |
| Sega 32X | `picodrive` | 20 | no |
| Atari 7800 | `prosystem` | 4 | no |
| SNES | `snes9x` | 43 | no |
| Atari 2600 | `stella2014` | 11 | no |
| Vectrex | `vecx` | 5 | no |


**825 options across 23 cores.** `fceumm` reporting zero is the one to check by
hand — NES cores normally have plenty, so it is more likely declaring them
through an API generation this host does not read than genuinely having none.

---

## The eight systems with a resolution dial

This is the part a quality setting would move. Everything else on this page is
either free, a matter of taste, or a correctness choice — see *What the levers
actually are* below.

### GameCube — `dolphin`

**`dolphin_efb_scale`** — Graphics > Settings > Internal Resolution

- default: `1`
- accepts: `1`, `2`, `3`, `4`, `5`, `6`

### Arcade (FinalBurn Neo) — `fbneo`

**`fbneo-resolution`** — Resolution

- default: `640x480`
- accepts: `640x480`, `800x600`, `1024x768`, `1080x810`, `1280x960`, `1440x1080`, `1600x1200`, `1920x1440`, `2160x1620`, `2880x2160`

### Dreamcast, Naomi — `flycast`

**`reicast_internal_resolution`** — Internal Resolution

- default: `640x480`
- accepts: `320x240`, `640x480`, `800x600`, `960x720`, `1024x768`, `1280x960`, `1440x1080`, `1600x1200`, `1920x1440`, `2560x1920`, `2880x2160`, `3200x2400`, `3840x2880`, `4480x3360`, `5120x3840`, `5760x4320`, `6400x4800`, `7040x5280`, `7680x5760`, `8320x6240`, `8960x6720`, `9600x7200`, `10240x7680`, `10880x8160`, `11520x8640`, `12160x9120`, `12800x9600`

### Nintendo 64 — `mupen64plus`

**`mupen64plus-43screensize`** — 4:3 Resolution

- default: `640x480`
- accepts: `320x240`, `640x480`, `960x720`, `1280x960`, `1440x1080`, `1600x1200`, `1920x1440`, `2240x1680`, `2560x1920`, `2880x2160`, `3200x2400`, `3520x2640`, `3840x2880`

**`mupen64plus-parallel-rdp-upscaling`** — (ParaLLEl-RDP) Upscaling factor (restart)

- default: `1x`
- accepts: `1x`, `2x`, `4x`, `8x`

### 3DO — `opera`

**`opera_high_resolution`** — HiRes CEL Rendering

- default: `disabled`
- accepts: `disabled`, `enabled`

### PlayStation 2 — `pcsx2`

**`pcsx2_upscale_multiplier`** — Video > Internal Resolution (Restart)

- default: `1x Native (PS2)`
- accepts: `1x Native (PS2)`, `2x Native (~720p)`, `4x Native (~1440p/2K)`, `8x Native (~2880p/5K)`

### PlayStation — `pcsx_rearmed`

**`pcsx_rearmed_neon_enhancement_enable`** — (GPU) Enhanced Resolution

- default: `disabled`
- accepts: `disabled`, `enabled`

### PSP — `ppsspp`

**`ppsspp_internal_resolution`** — Rendering Resolution

- default: `480x272`
- accepts: `480x272`, `960x544`, `1440x816`, `1920x1088`, `2400x1360`, `2880x1632`, `3360x1904`, `3840x2176`, `4320x2448`, `4800x2720`


### Nintendo 64 is the awkward one, and not because of the numbers

**It has two renderers and they are different emulators of the same chip.**

```
mupen64plus-rdp-plugin        angrylion | parallel | gliden64      (now: gliden64)
```

`gliden64` scales through `mupen64plus-43screensize`; **paraLLEl-RDP** has its
own `-upscaling` at 1x/2x/4x/8x and ignores the other. So "the N64 resolution
setting" is two settings behind a choice of renderer, and PROJECT.md already
warns why that matters beyond the picture:

> A graphics PLUGIN is a different thing from a graphics API… moving
> Mupen64Plus to parallel-RDP swaps the emulated RDP implementation.

N64's save states already do not restore exactly, with *"graphics-plugin state
that lives outside the state"* listed as one of three suspects. **Do not move
this one casually.**

---

## What the levers actually are

Sorting 825 options by what changing them costs, rather than by what they are
called. This is the finding that makes a single quality setting tractable.

### 1. Free, and simply better — turn them on and never show them

Measured on the A9 against a 6.02x-realtime baseline, PlayStation 2:

| | realtime |
|---|---|
| baseline | 6.02x |
| anisotropic filtering 16x | **6.09x** |

Inside the noise. Anisotropic filtering, bilinear texture filtering and
trilinear are free on any GPU this OS will meet. **They are not a quality tier,
they are the right answer**, and putting them behind one means somebody gets a
worse picture for nothing.

### 2. Correctness, and it belongs to the GAME — not to a tier

`pcsx2_blending_accuracy`, `pcsx2_vu_exact_mul`, the `*_softfloat` family,
Dolphin's EFB hacks. These are not "prettier", they decide whether a
particular title renders correctly at all. Cabinet for Mac already treats this
class as per-game and says why:

> which renderer a title needs is a fact about the title

### 3. Accuracy against compatibility — set once, never surfaced

`snes9x_overclock_cycles`, `genesis_plus_gx_overclock`, `melonds_jit_block_size`.
On any machine this OS runs on a SNES is not a performance problem, so there is
no trade to offer. Pick the accurate value and move on.

### 4. Look — free, and a matter of taste

`genesis_plus_gx_blargg_ntsc_filter`, `gambatte_gb_lcd_filter`, palettes,
scanlines. These belong wherever the shader and letterbox-glow settings end up,
**not** in a performance setting. Calling an NTSC filter "Quality" would be a
lie about what it does.

### 5. The dial — internal resolution, and essentially only that

Eight systems, one lever each (two for N64). It is the only class whose cost
scales with the setting and whose benefit is large.

---

## What it costs, and a warning about measuring it

PlayStation 2, Homura, 1800 frames each, on the A9's Radeon 890M:

| | emulated / wall | realtime |
|---|---|---|
| 1x native | 35.94s / 5.97s | 6.02x |
| 2x native | 35.94s / 15.41s | **2.33x** |
| 4x native | 35.96s / 7.06s | **5.09x** |

**Those numbers are wrong and they are printed here as a warning.** 2x cannot
be slower than 4x. That is shader compilation on the first run at a new
resolution, and it means **a single run is not a measurement** — anyone
building a tuning table needs warm runs and repeats, or they will tune against
their own shader cache.

The numbers taken with a warm cache earlier the same day, which are the honest
ones:

| | realtime |
|---|---|
| PS2 at 1x native | **10.1x** |
| PS2 at 4x upscale | **6.3x** |
| GameCube, Mario Kart | **4.6x** |

---

## What this means for open question 23

**The feature is possible, and it is one dial rather than a tier system.**
Only eight of twenty-three systems have anything to move, and for each of them
it is a single option. Everything else is set correctly once, per platform,
and never shown — which is work already committed to as open question 7.

**The obstacle is not the code and not the unknown hardware.** It is that there
is one machine to tune against and it has five to ten times the headroom it
needs. A Performance level tuned on hardware that never needs it is a guess
wearing a number.

**So the first version is the default, not the setting.** Eight systems, one
resolution choice each, chosen with a game on the television. The console
already reads its own speed off the core's audio against realtime, so it can
say *"this ran at 72%"* on a machine that cannot keep up — which is both more
useful and more console-like than a settings page nobody should have had to
find.
