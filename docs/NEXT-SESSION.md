# Picking this up

Written at the end of each session for whoever starts the next one, which is
usually a fresh assistant with no memory of what just happened.

**`docs/PROJECT.md` is the specification and it is authoritative. `docs/CABINET.md`
maps what Cabinet already solves.** This file is only the short version: what
state things are in, what to do next, and the handful of things that will waste
a day if nobody says them out loud.

Rewrite it at the end of a session. It is meant to be current, not a log.

---

---

**HOW THIS FILE IS ORGANISED, and the rule that keeps it usable.** It grew past
2,500 lines by being appended to across sessions, and by the end it carried two
answers to the same question in several places — a booted digest two deployments
old, a drop-in that had been deleted, an upscale nobody had chosen. Three kinds
of thing live here and they age differently:

| | |
|---|---|
| **State** — the first four sections | What is running, what is set, what is where. **Overwrite this every session.** It goes stale silently, and a wrong answer in it costs a day. |
| **Work** — the queue and the detail behind it | What to do next and why. When an item is done, move it to *Solved* rather than leaving it in the queue struck through. |
| **Lessons** — *Things that will bite you* | Accumulates and is never rewritten. These cost hours each and none is recoverable by reading the code. **Do not summarise one — a summary of a warning is not a warning.** |

**The records of being wrong stay, including the measurements that were taken
wrongly.** *Solved, and kept for the lesson*, at the back, holds finished work
with its investigations intact.

## Before anything else

**Everything is on `main`.** No other branches and no open pull requests.

**FIRST RUN IS BUILT, START TO FINISH, 2026-09-20.** A person can set this
console up with a keyboard and a phone and never touch SSH: network, Wi-Fi, the
RomM server, pairing by QR code, and a Bluetooth controller. Five screens and
six new files — see item 4b. All of it is checkable from a shell without
disturbing the television:

```
cabinetos-frontend --first-run          where setup is, and where it would stop
cabinetos-frontend --first-run-rules    96 fact combinations, asserting the refusals
cabinetos-frontend --network-scan       link, radio, polkit verdict, what is on the air
cabinetos-frontend --qr "<text>"        a code you can scan off a terminal
```

**The A9 correctly says it needs no setup**, because a machine that already has
an address, a token and a user is adopted rather than walked through a wizard.
That rule matters more than the marker file: the reference console was set up by
hand and must never be shown a setup screen.

**GAMESCOPE WILL COMPOSITE OUR MENU OVER A WINDOW WE DO NOT OWN — ANSWERED
2026-09-21, and it is a yes.** Item 1 has it and PROJECT.md open question 24 has
all of it. The one thing to carry in your head before you go near it:
**`gamescopectl screenshot` DOES NOT CAPTURE THE OVERLAY PLANES**, with any
type, so a capture showing "no overlay" means nothing at all. Look at the
television. Most of a session went to that.

**THE HANDOVER GOES IN THE WORK'S OWN PULL REQUEST.** Write it inside the
branch that does the work it describes, so there is never a handover-only push
and never a handover-only pull request. The rule and the one narrow exception
are in PROJECT.md, *Constraints and principles*, item 7.

**THE A9 MAX IS THE REFERENCE CONSOLE.** `cabinet@192.168.1.212`, same SSH key
as the VM, sudo password `cabinet`. It boots into the frontend on
**gamescope/drm** — the top compositor rung, which the VM has never reached —
on its own Radeon 890M at the panel's native **3840x2160**, with Vulkan
present (RADV STRIX1). **The UI freeze is lifted and what is on that television
is the real thing.** **1232 playable games**, PlayStation 2 and GameCube
included, off the image alone.

**IT RUNS THE IMAGE AND NOTHING BY HAND. THESE FIVE FACTS WERE READ OFF THE
MACHINE ON 2026-09-21, not carried across from an earlier paragraph** — every
one of them had a stale answer somewhere in this file and the machine settled
each one:

| Probe | Answer |
|---|---|
| `systemctl is-active cabinetos-session` | `active` |
| `ps -eo args \| grep [c]abinetos-frontend` | **`/usr/bin/cabinetos-frontend`** — the image's, under `gamescope --backend drm --output-width 3840 --output-height 2160` |
| `ls /etc/systemd/system/cabinetos-session.service.d/` | **empty. No drop-ins at all.** |
| `bootc status` | booted **`sha256:ceafc2bb…`**, with `sha256:ce25da80…` as the rollback |
| `journalctl -b -o cat \| grep '^\[ps2\]'` | `renderer Vulkan, **upscale 1x, anisotropy 0**` |

**THE TWO NUMBERS THIS FILE USED TO CARRY ARE NOW ONE.** It said "1147 on the
image, 1232 as it is running today", and the whole of that difference WAS the
drop-in: PlayStation 2 and GameCube are in the image now, so 1232 is both. (The
1232 is the audit's figure, not one recounted today; the audit's own totals need
a pass — see the note at the end of the platform audit.)

**`sha256:78e43b5a…` IS NOT ON THAT MACHINE AND HAS NOT BEEN FOR TWO
DEPLOYMENTS.** It was #30 and this file quoted it as the booted digest until
today. If a digest here disagrees with `bootc status`, `bootc status` is right.

**THE SESSION HAD BEEN DEAD FOR TEN HOURS AND NOTHING SAID SO.** It was found
`inactive` at the start of a session — stopped at 21:31 the night before and
never restarted, so the television had been showing nothing at all. Neither the
image nor a hand-built binary was running. **`systemctl is-active
cabinetos-session` is the first thing to check, before `ps`**, because a dead
session and a session running the wrong thing look identical to every other
probe on this page.

**IF SOMEBODY HAS PUT A DROP-IN BACK**, that is what
`/etc/systemd/system/cabinetos-session.service.d/20-heavy-systems.conf` was: it
pointed the session at `/var/home/cabinet/cabinetos-frontend-dev` and
`~/cores-dev`, which is how PlayStation 2 and GameCube reached the television
before they were in the image. There is no reason to want one now. Removing it:

```
sudo rm /etc/systemd/system/cabinetos-session.service.d/20-heavy-systems.conf
sudo systemctl daemon-reload && sudo systemctl restart cabinetos-session
```

**`Environment=` must be QUOTED** or systemd splits the value on whitespace and
silently drops every argument after the path — which looks exactly like a
console that ignored you.

**THE HAND-BUILT BINARY AND ITS CORE DIRECTORY ARE STILL ON DISK** —
`~/cabinetos-frontend-dev`, `~/cores-dev`, `~/assets-dev` — and nothing points
at them. They are for running something by hand; if you build a new frontend,
overwrite the existing one rather than adding a second. **A hand-run leaves
`[ps2]` lines in the journal from `/var/home/cabinet/cores-dev/`, which is how
to tell one apart from the session** — the session's say `/usr/lib/cabinetos/`.

**ALWAYS CHECK WHICH COMPOSITOR RUNG IT LANDED ON BEFORE JUDGING ANYTHING.**

```
journalctl -t cabinetos-session | grep 'is up'
```

`gamescope (drm) is up` is the real thing. `cage (software rendering) is up` is
llvmpipe and every visual judgement made on it is worthless — and the machine
gets there **on its own, from a boot-time network race**, while printing a
message that is not true. See item 4.

**THE IMAGE NOW CARRIES THE FRONTEND AND THE TWENTY-ONE CORES, 2026-09-19.**
This is the thing most likely to be wrong in anyone's head, because it was
untrue for a fortnight and a lot of text said so. Install CabinetOS on a
machine and it **boots into the frontend** with every emulator. It used to boot
to a black screen: `cabinetos-session` ran `sleep infinity`, because the image
contained nothing to run.

**THE FILES ON DISK MOVED, 2026-09-18.** There is no `romcache/` any more and no
`system/`. Games are in `roms/` and `cache/` under a platform folder, firmware
is in `bios/`, and every save, state and keep is under `users/<id> - <name>/`.
**There is no migration tool in the tree and there should not be** — nobody has
run CabinetOS outside of building it, so the one machine that needed moving has
been moved. Anything built from here starts on this layout.

Start from `main`, branch once, and **open the pull request against `main`**.
Four branches were once stacked on each other here, each opened before the last
had merged, and the result was three overlapping pull requests and a compile
check that did not apply to any of them. One branch at a time.

**Read `docs/CABINET.md` before designing anything.** Cabinet ships on iOS, tvOS
and macOS and has already answered most of what comes up here. tvOS is the
surface to copy, not iOS. Cabinet is not checked out on this Mac; clone it:

```
git clone --filter=blob:none --sparse https://github.com/MMagTech/cabinet.git
cd cabinet && git sparse-checkout set RommApp docs tools
```

`tools` is in that list because the per-core build scripts are the only honest
record of how a core is built — see *Cabinet-side debts*.

Then read `docs/PROJECT.md`, and `frontend/README.md` for the build loop.

**Two machines now.**

| | | |
|---|---|---|
| **A9 Max** | `cabinet@192.168.1.212` | the reference console. Real GPU, gamescope/drm, Vulkan. Judge the look here — and only here. |
| **Test VM** | `cabinet@192.168.1.250` | Unraid, no Vulkan, cage on llvmpipe. The dev loop and every headless measurement. |

Both take the key at `~/.ssh/cabinetos` and both have sudo password `cabinet`
— a throwaway, the same one the public repo's `disk_config/disk.toml` carries.
The RomM server they talk to is `192.168.1.10:6005`.

**Nothing builds on this Mac.** The frontend and the cores build in a container
on the VM and in CI. The VM loop is eleven seconds:

```
rsync -az -e "ssh -i ~/.ssh/cabinetos" frontend/src/ cabinet@192.168.1.250:~/frontend/src/
ssh -i ~/.ssh/cabinetos cabinet@192.168.1.250 \
  'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make'
```

## The state that lives on the A9 and not in git — new 2026-09-20

- `~/cores-dev/` — the 21 image cores SYMLINKED, plus **`cabinetos-ps2.so`**
  (upstream PCSX2 embedded, with `libryml` and `libc4core` beside it) and
  `dolphin_libretro.so`. This is what `--core-dir` points at.
- `~/assets-dev/` — **new 2026-09-21.** PCSX2's resources, which it refuses to
  start without, plus a symlink through to the image's own system files so the
  other cores keep theirs. `CABINETOS_ASSETS` points here because `/usr` is
  read-only on a bootc console.
- ~~`~/heavy/`~~ — **deleted 2026-09-21.** 859 MB of scratch holding three
  copies of the unshippable libretro PS2 core and duplicates of firmware the
  console already has properly under `/var/lib/cabinetos/bios/`.
- `/var/lib/cabinetos/bios/pcsx2/bios/` — the two PS2 BIOS files. **These come
  from RomM with the game on a real install** and are here by hand only because
  no image carries the core yet.
- `/var/lib/cabinetos/bios/dolphin-emu/Sys/` — Dolphin's 15 MB Sys folder. On a
  real install this ships with the core at `/usr/share/cabinetos/system/`, the
  way PPSSPP's already does.
- `~/cabinetos-frontend-dev` — the hand-built frontend the drop-in points at.
- `~/pcsx2-clean/pcsx2-upstream/` — **new 2026-09-21**, the upstream PCSX2
  checkout at v2.8.2 and its build tree, 375 MB. It is a **cache**:
  `cores/build-pcsx2.sh` re-clones and rebuilds it in 43 seconds, so delete it
  whenever. Point the script at it with
  `CABINETOS_CORE_SRC=/var/home/cabinet/pcsx2-clean`, or leave that unset and it
  makes its own under the repo.
- `~/pcsx2-lab/` — the PS2 BIOS and Homura's CHD, staged where a container can
  read them. **`:ro` bind mounts of `/var/lib/cabinetos` do not work** — SELinux
  denies the read and the container simply sees an empty directory, which reads
  as a missing BIOS. Copy to a scratch directory and mount that with `:Z`.
- `~/cabinetos-repo/` — **new 2026-09-21**, an rsync of this repository, because
  the build script has to run on a machine with podman and the Mac is not one.
  The A9 is now the better build machine by a distance: **24 cores, 25 GB of
  free RAM and 1.9 TB free**, against the VM's 5 cores, 3 GB and 4.1 GB. The
  whole PCSX2 library builds there in 25 seconds.

## The state that lives on the VM and not in git

- `~/frontend/` — the frontend source, built with
  `podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make`
- `~/frontend/cores/build/` — **twenty-one** built cores, where the frontend
  looks when it is run from there
- `~/frontend/` is **still the storage root for a build run by hand from that
  directory**, and that is the dev loop. **It is no longer the session's root**:
  the VM was upgraded onto the image that carries the console on 2026-09-19,
  so `tmpfiles.d` now creates `/var/lib/cabinetos` and the session uses it.
  Two consoles' worth of tree on one machine, deliberately. `~/frontend` holds:
  - `bios/` — BIOS fetched from RomM, files the cores write into their system
    directory, and **`PPSSPP/`**, 13 MB of PSP system files that came out of the
    core build. Copy it from `~/cabinetos/cores/system/` after building that
    core. **On a console this one comes from the image instead**, at
    `/usr/share/cabinetos/system/`, and is symlinked in at startup.
  - `cache/<platform>/` — downloaded games, and the only thing eviction touches
  - `roms/<platform>/` — kept games. Also still holds one loose
    `Dr. Mario (World) (Rev 1).gb` for `--core`.
  - `users/1 - MMagTech/` — every save, state, keep and unsent upload, plus
    `saves/unattributed/`, which is the two old shared save piles kept whole
    because nothing in them says which game wrote them.
  - `config/user.json` — the RomM user id and name, cached so a console with no
    network still knows whose saves it is holding.
  - `config/drives.json` — which games drives were here last time, and the ONLY
    thing about storage that is remembered rather than read off the disk.
- `/var/mnt/games/CabinetOS/` — **the VM's games drive**, claimed automatically,
  holding its own `roms/` and `cache/`. The console claims that one folder and
  nothing else on the disk.
- `~/cabinetos/` — a clone of this repo, where `cores/build-core.sh` runs. Its
  `cores/build/` has twenty, not twenty-one.
- `~/cabinetos/.core-src/` — **now a SYMLINK to `/var/mnt/games/core-src`**,
  moved there 2026-09-20 because `/var` had fallen to 605 MB free and the
  builder container would not rebuild. Same 4.8 GB, of which PPSSPP is 3.4 GB,
  on the disk that has 65 GB. Still a cache: delete any of it to make room and
  the next build re-clones. `/var` is back to about 5 GB free.
- `~/run-frontend.sh` — the session launcher, used via `CABINETOS_APP`. The
  original is `run-frontend.sh.bak`. **The drop-in that pointed the session at
  it was moved aside on 2026-09-19** to `~/10-frontend.conf.disabled`: its own
  comment said it goes away once the binary ships in the image, and it does.
  Put it back if you want the session running a hand-built frontend.
- `/etc/cabinetos/session.env` — the RomM address, added 2026-09-19. Same file
  as on the A9.
- `~/.config/cabinetos/romm.json` — the RomM token, 0600
- `/var/mnt/games/flatpak/` — **a flatpak user installation holding RPCS3**,
  2.7 GB, reached with `FLATPAK_USER_DIR=/var/mnt/games/flatpak`. On the games
  disk deliberately: the KDE runtime it needs is 1.1 GB and `/var` has 5 GB.
- `/var/mnt/games/layout-backup/` — a tar and a `sha256sum` list of every save
  the VM held before the folder layout moved them. Belt and braces; 11 MB,
  delete it whenever.
- `/var/mnt/games/ps3lab/` — the PS3 experiment. 195 MB of firmware, two
  installed games, both `.rap`s, and the PUP. RPCS3's renderer is set to
  **Null** because the VM has no Vulkan.

### The VM has TWO disks, and the second one is the point

| | |
|---|---|
| `/dev/vda4` → `/var` | 21.6 GB btrfs, **about 5.7 GB free**. The OS and everything above. |
| `/dev/vdb` → `/var/mnt/games` | **100 GB btrfs**, label `cabinetos-games`, about 65 GB free. |

In `/etc/fstab` by UUID with `nofail`, and **proved across a reboot**. `nofail`
matters: a machine that will not boot because a games drive is missing is
exactly what open question 14 forbids.

**One of its four purposes is still untested.** *That a missing drive degrades
rather than errors* cannot be exercised yet: `storage::locations()` returns one
location, so there is no second one to remove. It becomes testable the day open
question 14's second location is wired in.

**`disk_config/disk.toml` still says `minsize = "20 GiB"`**, so a VM rebuilt
from a fresh qcow2 comes out small again with no second disk. That number
should change; it is a one-line edit nobody has made.

`podman image prune -f` is still the first thing to try when `/var` gets tight,
then `~/cabinetos/.core-src`.

## Where things stand

**The whole loop works, the whole library is reachable, every emulator this
console ships can be run, and all of it is now in the image.** Browse every
system and collection, open a game, play it or download it, save and load
states, and leave — with the save syncing on the way out.

- **1147 of 1644 games playable, with all twenty-one cores built** — and every
  one of the twenty-one can be RUN, not just built.
- **The image is the console**, as of 2026-09-19: `/usr/bin/cabinetos-frontend`,
  `/usr/lib/cabinetos/cores/` (21 cores, 259 MB) and
  `/usr/share/cabinetos/system/` (PPSSPP's 13 MB). 273 MB added. Every image
  build now also asserts all twenty-one pinned core revisions, because it calls
  `build-core.yml` rather than repeating it.
- **Home is a top bar, Recent and Favorites**, lit by whatever game focus is on.
  The hero card was removed on 2026-09-21 — see the UI section for what it was
  and why. The first card on Recent is the game you were playing and A launches
  it straight.
- **Library, a grid, a launch screen and Search.** Every system including the
  ones this console cannot play, each saying why. The navigation bar is on all
  of them and L1/R1 walk between destinations.
- **Download is the one deliberate storage act**, and it keeps the game. The
  cache stays invisible; Play fetches silently and says nothing. **"Remove
  download" removes it and gives the space back.**
- **The files are somewhere a person can find them**: `roms/`, `cache/`,
  `bios/`, and a folder per person holding their saves and states. Keeping a
  game is a decision per person rather than a flag on it.
- **Plug a second drive in and it is used** — internal or USB, no setup screen,
  one folder claimed on it and nothing else touched.
- **Both floors are enforced where that button is**, measured by filling the
  disk rather than by reasoning about it.
- **Saves, memory cards and states sync both ways** with RomM, tagged with
  Cabinet's own emulator strings — and as of 2026-09-19 **one of them was
  written by a person playing a game**, not by a round-trip test.
- **Vertical arcade games play the right way up**, as of 2026-09-19. Half the
  arcade library was on its side; a TATE board now fills the height of the
  screen in a tall window with the glow on the wide bars either side.
- **EVERY platform this console claims to play, plays** — measured 2026-09-19
  by launching the smallest game on each of twenty-six platform rows and
  reading the maximum pixel of the frame. It found three faults and all three
  were silent.
- **EVERY platform's saves travel**, not just the ones whose core exposes a
  battery. The 47 of 81 rows on the server that could neither be uploaded nor
  restored now do.
- **BIOS comes down with the game**, every file the platform lists — except
  PSP's, which is not a console's firmware and ships with the emulator.
- **Dreamcast, Naomi, N64 and PSP play**, through a framebuffer inside the
  frontend's own GLES context, with no pixel read back anywhere — **except that
  DREAMCAST NO LONGER LAUNCHES ON THE A9**, found 2026-09-21. It is a regression
  and the console says why: the device will not export memory as a file
  descriptor. N64 still launches on the same boot, so it is Flycast's path and
  not all of hardware rendering. See item 9d.
- **The interface makes sounds**, synthesised rather than recorded, with an off
  switch waiting for a Settings screen to own it.
- **First run's mechanisms exist and none of them is a picture**, as of
  2026-09-20: `firstrun.{h,cpp}`, `qr.{h,cpp}`, `net.{h,cpp}` and a polkit rule.
  The chain is enforced rather than described — `--first-run-rules` walks every
  combination of facts and asserts the REFUSALS, and it found a real deadlock on
  its first run. **The screens themselves still wait for the look.**

## Pick up with these, in this order

### WHAT TO DO NEXT

**THE UI PASS HAPPENED on 2026-09-21 and this entry is what it left.** It was
asked for as *"i want the next session to be ui focused so we can tweak it"*,
and it ran — about forty builds, each looked at on the panel. **What is queued
now is the SETTINGS screen**, which is the last bar item that does nothing and
the only place open question 23's quality control and the interface sounds'
off switch can live.

**Read *A pass over the whole UI* further down this file before anything else** —
it has what exists, what the pass decided, and the short list it left. Two items
on that list are decisions rather than drawing: whether the cartridge-era cores
keep "Save state" in the pause menu (open question 25), and the one quality
setting (open question 23). Everything below is still true and still queued; none
of it is what to open tomorrow.

**THE ONE THING THAT IS NOT MINE TO FINISH** is PlayStation 2's first real
in-game save reaching the server, which needs somebody to play. It is item **U0**
below and it takes ten minutes of somebody's evening, not a session.

| | |
|---|---|
| **U** | **THE UI PASS — START HERE.** Keep the Cabinet look, take the lessons from SteamOS. See *A pass over the whole UI*. |
| **U0** | **PLAY BURNOUT 3, SAVE INSIDE THE GAME, AND EXIT TO HOME.** The whole save path is proved EXCEPT the upload, and the upload has never once been watched for PlayStation 2. Restore works — 8.6 MB comes off the server and PCSX2 reads the card as `Formatted`. The freshness rule means an unchanged card is correctly NOT sent, which is why no test here can stand in: **it needs a card that actually changed.** Look for `[save] uploaded pcsx2` in the journal, and for rom 604's row on the server to move off its 2026-09-11 timestamp, which is the Mac's. |
| **0** | ~~WILL GAMESCOPE COMPOSITE OUR OVERLAY?~~ — **ANSWERED YES, item 1.** It composites, the game shows through our transparency, and our overlay takes the pad and gives it back while the game keeps the screen. **What it saves is now MEASURED too** — free at 3x, ~1.9 ms at 4x, ~9 ms at 6x — so the remaining step is a DECISION rather than a test. Two paths to the screen are **accepted** (open question 24), and the menu already works on both. Nothing is built. |
| **0a** | ~~EMBED UPSTREAM PCSX2~~ — **DONE AND PLAYING, see item 1a.** Upstream builds as a library on Linux with **no patches**; `cores/build-pcsx2.sh` reproduces it in 43 seconds. What is left is the host layer — 55 `Host::` functions and four other symbols, most of them one-liners, with **six that are real work** and all six in the display path the Vulkan host already serves. **Write it: there is no cheaper step in front of it**, and gsrunner cannot stand in because it only replays GS dumps. |
| **0b** | ~~THE TWO BUGS IN ITEM 1b~~ — **BOTH CLOSED 2026-09-21.** The tunnel went with the move to upstream PCSX2. The exit hang is recorded as **not reproduced**, not fixed, so if it returns the suspect in item 1b is still where to look. ~~GameCube's core can be pinned~~ — **PINNED AND BUILT BY CI**, `dolphin` at `1a0f97270b70`, merged as #43. |
| **1** | ~~PLAYSTATION 2 AND GAMECUBE~~ — **done to the point of playing**, see above. The original entry follows for its reasoning. **PLAYSTATION 2 AND GAMECUBE.** MMagTech's call, 2026-09-20, and the largest thing on this list: 85 games, and the only missing tier with a working implementation to copy. **Open question 12b has the order and 12 has the numbers.** Start by reading `tools/build-dolphin-mac.sh` in Cabinet — those two are NOT libretro cores and nobody wrote down why. |
| **2** | ~~A GAME CAN GO BLACK AND NOBODY KNOWS WHY~~ — **SOLVED 2026-09-21, item 3b.** It was the SECOND game: `Core::load` left the previous game's dimensions behind, so `sizeChanged` came out false and the upload hit a texture with no storage. Confirmed by MMagTech on TurboGrafx and 3DO. Nothing is owed here. |
| **3** | **Judge the TATE look, and Home, on the 65-inch.** Both are on the machine and neither has been looked at properly. **This is really part of the UI pass** and should be done inside it rather than as its own errand. |
| **4** | **Atari Jaguar and ColecoVision** — 73 games, ordinary libretro cores, no architectural question at all. The cheapest games available. See 12b. |
| **5** | Then the core options (item 7). |

**FIRST RUN IS DONE and is not on this list.** Built, walked on the television
with a keyboard, a controller and a mouse, and the console was set up from
nothing with no cable in it. See item 4b for what is left, which is small.

**Item 2's old entry is gone because it is fixed**: the console no longer demotes
itself to software rendering on a boot-time network race. The A9 has come up on
`gamescope (drm)` on every boot since.

**Search and Settings** are drawn in the top bar and say "not built yet"; that
is deliberate and can stay.

### 1. GAMESCOPE WILL COMPOSITE OUR MENU OVER A WINDOW WE DO NOT OWN — ANSWERED 2026-09-21

**Yes, on all three counts, proved on the reference console's own television at
3840x2160.** This was the question at the top of this file and it is settled.
The whole thing is written up in PROJECT.md, open question 24; this is the short
version and the traps.

| | |
|---|---|
| Composites a window we do not own | **Yes.** glxgears drew over a vkcube it has no relationship with. |
| The game shows through our transparency | **Yes.** MMagTech, looking at the set: *"i can see through the green"*. |
| Our overlay takes the pad, and gives it back | **Yes**, one atom, while the game keeps the screen. |

All three confirmed together in the `STEAM_OVERLAY` slot, MMagTech watching the
set: *"yes magenta is therre and i can see throught the green bar and the cube
keeps spinning"*. **The cube still spinning is the part that matters** — the game
goes on rendering while the overlay holds the pad.

The input result is exactly a pause menu:

```
Global focus window:          0x400000 (Vkcube X11)            <- game keeps presenting
Global input focus window:    0x600002 (cabinetos overlay probe)
Global keyboard focus window: 0x600002 (cabinetos overlay probe)
```

Reproduce it with `tools/gamescope-overlay-test.sh steam`, on the A9. It borrows
the television and puts it back on every exit path.

#### THE SLOT IS NOT THE OBVIOUS ONE

`GAMESCOPE_EXTERNAL_OVERLAY` is the one everything documents and it is **wrong
for this**. It composites and it can never take input — gamescope grants input
focus only under `if (w->isOverlay && w->inputFocusMode)`, and `isOverlay` is the
`STEAM_OVERLAY` atom. External is the HUD slot; mangoapp lives there and a HUD
never needs the pad. **A pause menu has to be `STEAM_OVERLAY` + `STEAM_INPUT_FOCUS`**
— the slot Steam's own overlay uses to draw over a game it does not own.

#### THE TRAP THAT COST MOST OF THE SESSION

**`gamescopectl screenshot` DOES NOT CAPTURE EITHER OVERLAY PLANE.** Not the
default type 1, and not type 2, whose own description says "the game +
overlays". A capture taken with a working overlay on screen comes back showing
only the game. Hours went into chasing an overlay that was on the television the
whole time, and it ended because MMagTech looked up and said the gears were in
the top left.

**There is no capture path that shows this. Look at the television, or
photograph it.** Same lesson as "judge the look only on the A9", in a new
costume: the instrument was lying, and lying plausibly.

Two more that each look like a compositor refusing:

- **Both slots are read only from the ROOT Xwayland context.** A window on any
  other server carries the atom correctly and is silently never consulted.
- **The atoms must be set BEFORE the window maps.** Afterwards sets a flag on a
  window nothing re-examines. Poking `_NET_WM_WINDOW_OPACITY` forces the rescan,
  which is a testing lever and not a design.

And the overlay is painted **`NoScale`**, at its own pixel size — the frontend
would render its menu at the panel's full resolution itself. glxgears landing
300x300 in the corner of a 4K screen is what getting that wrong looks like.

#### THE MENU ALREADY DRAWS CORRECTLY ONTO NOTHING — 2026-09-21

`cabinetos-frontend --overlay-test` puts the REAL pause menu up as a gamescope
overlay over a game the console did not draw. MMagTech, watching: *"everything
looks pretty solid except the menu itself is not glass."*

**The risk that could have killed this is gone.** Clean text, clean edges, no
premultiplied-alpha haloing, scrim dims correctly. **It needed no shader work** —
the blend function was already right, so the change is two clears going to alpha
0 and one black fill being skipped.

**The one casualty is the glass panel.** `drawGlass` blurs by sampling our own
scene texture; on this path the game is on another plane and is not in it.

**gamescope's own blur was tried and did not work** — `GAMESCOPE_BLUR_MODE` 1 and
2, with radius, fade and forced composition. `composite_debug` proved gamescope
really was compositing. **Abandoned, not solved**; do not record it as
unavailable.

**The route to try is one grab at the moment of pause, and the ingredient is
proved:** a base-plane screenshot taken WHILE the overlay is up returns the game
alone, clean, at full resolution. Grab once when the menu opens, blur it
ourselves, use it as the panel's backdrop. The game is paused so a still is
correct, and the panel already fades in over 350 ms.

#### WHAT IT SAVES — MEASURED UNCAPPED, WHICH MAKES THIS TABLE UNSAFE

**READ THE WARNING BEFORE THE TABLE.** These numbers are UNCAPPED, and item 1a
later establishes — on the same game, on the same machine — that uncapped runs
understate the readback by more than half, because the emulator runs flat out
and the readback hides inside its slack. Capped to 60 Hz, which is how a person
plays, 4x costs 6.1 ms rather than the 1.9 ms this table credits it with.

**SO THE CONCLUSION THIS SECTION USED TO DRAW — "at 3x it is free, do not sell
compositing on PS2" — IS NOT ESTABLISHED.** Capped, 3x readback costs 5.0 ms
average and 7.3 ms worst, so there may be a great deal to save at exactly the
upscale this file calls the sweet spot. It may still be true that compositing is
a weak case for PlayStation 2; nothing here shows it either way any more.
**Re-measure capped before anybody decides on this.**

**THIS IS THE THIRD TIME THIS PROJECT HAS MEASURED IN A CONFIGURATION NOBODY
PLAYS IN**, after the cold shader cache and the unpaced frame loop, and it is
kept rather than deleted because the pattern has now cost more than any one of
the numbers. The warm-cache rule was already written down; "and pace it the way
it actually runs" is the other half of it, and it had to be learned twice.

Burnout 3, warm cache, **uncapped**, `CABINETOS_PS2_NO_READBACK` against normal.
4x run twice, reproduced within 3%.

| Upscale | readback on | off | saved |
|---|---|---|---|
| 3x | 3.36 ms | 3.25 ms | **0.11 ms — free** |
| 4x | 5.08 / 5.21 ms | 3.21 / 3.25 ms | ~1.9 ms |
| 6x | 13.16 ms | 4.17 ms | **~9.0 ms** (76 → 240 fps) |

**AT 3x IT MEASURES FREE HERE** — 1.95 ms of work costing 0.11 ms, because it
overlaps with the emulator's other threads. **That overlap is exactly what
disappears when the emulator is paced to 60 Hz and has no slack to hide in**, so
this row is the one the warning above is really about. It was read as
"compositing buys PlayStation 2 nothing"; it does not support that.

**The case is true 4K and the systems that do not exist yet**, which is
MMagTech's argument and the numbers back it better than they back the PS2 one.
The readback is free at 3x *because PCSX2 runs at 300% and has slack to hide it
in*. A PS3 at native 1080p pushes what a PS2 pushes at 3x, with no slack at all.

#### WHAT THIS DOES NOT SETTLE, AND IT IS MOST OF THE WORK

**Nothing has been built.** One test program and a stand-in game. The frontend
has not been split, PCSX2 has not been given a window, no emulator has run this
way, and our own renderer has never drawn into a transparent surface.

**NOBODY HAS MEASURED WHAT COMPOSITING ITSELF COSTS**, which is the other half
of the sum and the half that decides it. 6.1 ms is what the CURRENT path costs,
capped; the table above says what removing the readback saved in an uncapped run
and is not to be trusted for this. The compositing path's own cost has not been
measured at all. It should be near
nothing — and *should be* is how this project has been wrong before, twice, in
exactly this area.

**THE PRICE IS TWO PATHS TO THE SCREEN, not the atoms.** The twenty-one libretro
cores must NOT move: there the frontend creates the device and lends it to the
core, which renders into a texture we already own, and nothing is copied.
Dreamcast, N64 and PSP have never paid this cost. So taking this route means the
console keeps one path for cores and gains another for standalone emulators. That
is the thing to weigh, and it is a design decision rather than a measurement.

**It applies to every heavy system, which is why it was worth doing first.** PS3,
Switch, Xbox and Wii U are all standalone emulators of exactly this shape and
MMagTech has said all of them are coming — open question 12b.

#### AND THE PATCH DECISION IS STILL WORTH REVISITING, SEPARATELY

Open question 12b records MMagTech ruling out a second patch to PCSX2, and that
decision stands on its own reasoning. **But it was made on a cost I described
wrongly** — "true 4K is expensive" rather than "this may be slower than the same
machine on Windows". A decision made on bad information is worth putting back in
front of him with good information. **That is not the same as reinterpreting a
settled decision because an easier path runs**, which this project forbids and
was burned by a session ago.
---

### 1a. PLAYSTATION 2 PLAYS FROM UPSTREAM PCSX2, IN THE CONSOLE — 2026-09-21

**PLAIN VERSION: pick a PlayStation 2 game on the television and it plays, on
upstream PCSX2 2.8.2, inside the console — your library, your pause menu, your
pad, your saves.** Not a separate program borrowing the screen. Verified under
gamescope on the reference console at 3840x2160, not only headlessly.

**IT IS ON THE TELEVISION RIGHT NOW, OFF THE IMAGE** — this said "through the
drop-in described at the top of this file" and that was true for a few hours of
2026-09-21, before the emulator shipped. There is no drop-in; see the table at
the top. Two lines in the journal say what is actually running, and both are
facts rather than restatements of what was asked for:

```
[ps2] /var/home/cabinet/cores-dev/cabinetos-ps2.so (PCSX2 v2.8.2)
[ps2] VM started, renderer Vulkan
```

**THE SECOND ONE EXISTS BECAUSE ASKING FOR VULKAN AND GETTING IT ARE DIFFERENT
THINGS.** PCSX2 falls back to its software renderer when a device cannot be
created, and a software PlayStation 2 on a machine with a Radeon in it looks
like nothing at all until somebody wonders why a game is slow. This console has
been caught by a silent fallback twice already — a compositor reporting "no
Vulkan-capable GPU" four seconds after selecting one, and a core that
substitutes its own boot ROM and says nothing at any log level. If it ever says
`software` it also says, in capitals, that that is not what was asked for.

Confirmed on the reference console, 2026-09-21: `AMD Radeon 890M Graphics
(RADV STRIX1)`, `Vulkan 1.4.354`, with a Vulkan shader cache warming across
launches.

**THE LIBRETRO PS2 CORE IS DELETED FROM THE REFERENCE CONSOLE — ALL FOUR
COPIES.** Not moved aside: deleted. MMagTech's call, 2026-09-21, and it is the
right one.

It can never ship, because `libretro/pcsx2` does not exist and so it cannot be
pinned, built in CI or audited. **Keeping a WORKING copy on disk leaves armed
exactly the trap that cost the last session**, which reinterpreted this
decision because a libretro core happened to run — and this page already says
in capitals that a working binary is not authority to change route. A binary
nobody can reproduce, sitting next to one they can, is an invitation.

`~/heavy/` went with it: 859 MB of last session's scratch, holding three more
copies plus duplicates of the PS2 BIOS and Dolphin's Sys folder that the
console already has properly under `/var/lib/cabinetos/bios/`.

**There is now exactly one PlayStation 2 emulator on that machine.**
`catalog::coreFileName` resolves `ps2` to `cabinetos-ps2.so` and nothing looks
for the other name.

#### What was built, and how to rebuild it

```
cores/build-pcsx2.sh          the emulator: libpcsx2.a, then cabinetos-ps2.so
cd frontend && make           the console, which dlopens it
```

43 seconds for the first from a clean clone on the A9.

| | |
|---|---|
| `frontend/ps2/CabinetPS2Host.cpp` | all 55 `Host` functions, the VM lifecycle, the frame readback, the pad translation |
| `frontend/ps2/CabinetPS2Audio.cpp` | SPU2's samples, handed to the console instead of to a sound card |
| `frontend/ps2/CabinetPS2Bridge.cpp` | the flat C face the console `dlopen`s |
| `frontend/ps2/CabinetPS2Probe.cpp` | the headless harness, for measuring without a television |
| `frontend/src/ps2.{h,cpp}` | the console's side of that wire |
| `frontend/src/core.cpp` | seven small branches, and nothing above them changed |

#### The shape, and why it is this one

**PCSX2 NEVER GETS A WINDOW.** The frontend owns the one window there is, draws
every screen in it and draws the overlay on top — which is what makes Pause and
Exit to Home work the same for a PlayStation 2 game as for a Mega Drive one.
**Save state is deliberately NOT on that list**: open question 25 decided PS2
does not get one. The point stands without it — one window, one overlay, one
way out.

So PCSX2 runs **surfaceless on its own thread** and hands over the finished
frame as a buffer of pixels, a width and a height — **exactly what eighteen of
the twenty-one libretro cores already give the frontend.** That is why
PlayStation 2 needed no new picture path in the UI and why `Core::texture()`
and `frameUV()` did not change at all.

**Measured on Burnout 3, because the design rests on it:**

| Upscale | Frame | Readback | Of a 60 Hz frame |
|---|---|---|---|
| native | 640x448 | **690 us** | 4.1% |
| 4x | 2560x1792 | **3043 us** | 18.2% |

The emulator ran at about 500% of realtime throughout. The number is live in
`CabinetPS2::Metrics::readback_us`, because a high upscale on a weaker machine
is what would change the answer. **The faster route is written up and
deliberately not taken**: PCSX2's Vulkan image could be shared directly, the way
`vkhost.cpp` already shares one, but its required device extension list holds
one entry and none of the external-memory ones. **It is four lines and it is
RULED OUT** — MMagTech declined a second PCSX2 patch on 2026-09-21, *"id rather
not have to patch and then maintain them"*, open question 12b. An earlier
version of this sentence said it was "worth spending the day a measurement says
the readback is too slow"; the measurement then came in at 6.1 ms and the
decision still stands. **Do not treat the number as permission.** If it is to be
revisited it is revisited with MMagTech, as a decision, not as a consequence of
a benchmark.

#### One patch to PCSX2, and the headline is corrected rather than dropped

**"Upstream builds as a library with NO patches" was about the BUILD and is
still true.** There is now exactly one patch and it is not needed to build
PCSX2 — only to stop it making its own sound. Three lines in
`AudioStream::CreateStream`, asserting its own anchor the way every patch in
`cores/build-core.sh` does. Cabinet makes the same edit on the Mac.

**The console owns audio and input, as it does for every other core.** One
device, one volume, one latency, an overlay that can duck it, and one path onto
the pad.

#### The memory card needed no new machinery at all

`catalog::saveFiles` already says a PlayStation 2 card is `<stem>.ps2` in the
per-game save directory, and `filesave.cpp` already restores it before launch,
captures it after, refuses to upload an unformatted one, and files it on the
server under the name Cabinet's Mac uses. **Pointing PCSX2's memory-card folder
at that directory was the whole of it.**

The name is derived inside `Core::loadGame` from the rom path rather than passed
in, so the two cannot drift — a card written under a name the save layer does
not look for is a save that never reaches the server, and nothing would say so.

**Burnout 3 is rom 604 and its card is the only real PlayStation 2 save on the
server.** Nothing in this work went near it: every test used Homura.

#### PICTURE QUALITY: 4x LOOKS RIGHT AND STUTTERS A LITTLE — 2026-09-21

**Played on the television. MMagTech: "that looked way better maybe we pushed
it a bit too aggressive though as i did notice some stuttering."** Deliberately
not chased — his call — but the leads are here so it is cheap to pick up.

`--ps2-upscale N` and `--ps2-aniso N` exist **as a TEST INSTRUMENT AND NOT THE
PRODUCT**, and main.cpp says so beside them. How this is really exposed is open
question 23 — one quality setting for the whole console. Nobody should build a
settings screen on these two flags.

**THE REFERENCE CONSOLE IS AT 1x AND ANISOTROPY 0 — checked on the machine
2026-09-21.** This paragraph used to say `--ps2-upscale 4 --ps2-aniso 16` was
"left there deliberately at the end of the session"; it was, and then the
drop-in carrying those flags was deleted when PlayStation 2 went into the image,
which reset both to their defaults. **Nothing chose 1x.** The journal is the
check: `[ps2] renderer Vulkan, upscale 1x, anisotropy 0`.

**WHAT MMagTech ACTUALLY PLAYED AND LIKED WAS 4x**, and he noticed some stutter
at it. The reason not to simply put 4x back is item 1: **the stutter may be this
console's own picture path rather than the machine running out of room**, and
setting it either way by hand hides the question rather than answering it. This
is open question 23 arriving with a face on it — those two flags were always a
test instrument and nothing yet exposes quality as a real setting.

**MEASURE IT CAPPED TO 60 Hz, NOT UNCAPPED, AND THE FIRST TABLE HERE WAS WRONG
FOR EXACTLY THAT REASON.** Uncapped, the emulator runs flat out, the readback
overlaps other work and hides inside it; capped — which is how a person plays —
it costs more than twice as much. The same run, same game, same upscale:

| Burnout 3, 4x | readback average | worst |
|---|---|---|
| uncapped, 602% of realtime | 2.7 ms | not measured |
| **capped to 60 Hz, how it is played** | **6.1 ms** | **12.2 ms** |

**THE WHOLE CURVE, capped, warm cache, Burnout 3:**

| Upscale | Internal | average | worst | of a 60 Hz frame |
|---|---|---|---|---|
| 1x | 640x448 | 0.8 ms | 4.5 ms | 5% |
| 2x | 1280x896 | 2.3 ms | 5.5 ms | 14% |
| **3x** | 1920x1344 | **5.0 ms** | **7.3 ms** | 30% |
| 4x | 2560x1792 | 6.1 ms | **12.2 ms** | 37% |

**3x IS THE SWEET SPOT AND THE REASON IS THE WORST CASE, NOT THE AVERAGE.**
Going 3x to 4x buys 1.1 ms of average and costs **5 ms of worst case** — the
spikes nearly double while the mean barely moves. Stutter is a worst case, so
that is the column to read. 3x is also 1920x1344, close to what Cabinet renders
at on the Mac.

**"Roughly proportional to pixels" was a guess and it was wrong.** The cost is
worse than linear at 4x and it shows up in the spikes rather than the mean.

**AND ALL OF IT IS A FLOOR RATHER THAN A CEILING**: measured in Burnout 3's
attract mode, not in a race. Real play is heavier.

**6.1 ms is 37% of a frame budget and the worst case is 73% of one.** Nothing
dropped a frame in that run, but it was a menu rather than a pile-up, and there
is very little room left. **That is a plausible cause of the stutter MMagTech
felt and it should be treated as the leading suspect.**

The lesson is the one this project keeps relearning in a new costume: a
measurement taken in a configuration nobody plays in is not a measurement of
the product. The warm-cache rule was already written down here; "and pace it
the way it actually runs" is the other half of it.

**5x IS THE FIRST GENUINELY 4K VALUE.** A PlayStation 2 renders 640x448 and a
4:3 picture on a 3840x2160 panel is 2880x2160 of real screen, so the arithmetic
is 2160/448 = 4.8. Below that the panel is stretching.

**THE STUTTER IS PROBABLY THE READBACK ITSELF, and an earlier version of this
paragraph said the opposite off the uncapped number.** At the pacing a person
plays at, getting the picture off the GPU costs 6.1 ms on average and 12.2 ms
at worst, out of 16.7. Two other suspects remain but neither is first:

1. **The frame handover copies 22 MB per frame under a lock the GS thread also
   wants.** `CabinetPS2::TakeFrame` copies rather than lends, deliberately, so
   the frontend cannot hold a buffer the emulator is overwriting — but at 4x
   that copy is 2730x2048x4 bytes, sixty times a second. A double buffer makes
   it a pointer swap. **It is worth doing and it will not be the fix**: it
   removes the smaller half, not the 6 ms.
2. **Shader compilation.** PCSX2 compiles pipelines as new effects appear, and
   Burnout 3 in traffic is where they appear. The cache is per game and warms
   up, so the second run through the same area is the test.

**THE ONLY THING THAT REMOVES THE 6 MS IS THE PATCH THAT IS RULED OUT**, so the
lever that is actually available is the upscale itself. Lowering it is not a
consolation prize — 3x costs about 5.0 ms against 4x's 6.1 ms average, and 7.3
against 12.2 at worst. **(This sentence used to say "roughly proportional to
pixels, so 3x is about half of 4x", four paragraphs after the sentence
establishing that proportionality was a guess and was wrong. It is the worst
case that moves, not the mean.)**

**AND THE READBACK SCALES WORSE THAN THE PIXELS.** 4x to 6x is 2.25 times the
pixels and **4.6 times the cost**, which is a wall rather than a curve.

**DO NOT ANSWER THAT BY PATCHING PCSX2. DECIDED 2026-09-21.** Sharing PCSX2's
Vulkan image instead of copying the picture through the CPU would need two
device extensions upstream does not enable, and MMagTech has ruled out carrying
a second patch: *"id rather not have to patch and then maintain them."* The
audio patch stands only because PCSX2's backends are a fixed list and there was
no other way. PROJECT.md, open question 12b, has the whole decision and the cost
it accepts — **4x is the practical ceiling and true 4K stays expensive.**

**The thing to try instead needs no patch**: the frame handover copies about
22 MB per frame at 4x under a lock the emulator's own thread also wants, and a
double buffer makes that a pointer swap. Entirely our own code.

#### WHAT IS NOT DONE, and none of it is hidden

- ~~**NOBODY HAS PLAYED IT WITH A PAD YET.**~~ **PLAYED, 2026-09-21**, and it
  found three faults nothing here had caught — see below. What is still owed is
  a long session rather than a first look.
- **PLAYSTATION 2 HAS NO SAVE STATES AND IS NOT GOING TO — DECIDED 2026-09-21,
  OPEN QUESTION 25.** `Core::stateSize()` returns 0 for PlayStation 2 and says
  so honestly, and **that is the finished answer rather than work owed.** An
  earlier version of this entry said "whatever this console does there is new
  work"; MMagTech asked the question before that one — *"should these more
  modern system even have save and load states or just the memory cards"* — and
  the answer for PS2, GameCube and everything after them is no.

  **THE REASON THAT SETTLES IT ON ITS OWN: a PCSX2 state is tied to the emulator
  build, so pushing an image would silently stop everyone's states loading.**
  That is data loss on this project's release schedule rather than on the
  player's. The other two reasons are that these machines have real save systems
  every game uses, and that a state from this build opens on no other machine
  while the library is meant to travel. **Memory cards are the save story here
  and they already work.**

  **DO NOT GO AND MAKE PCSX2 SERIALISE TO MEMORY.** It is the obvious next step,
  it is why this entry used to point at open question 12b, and it is the step
  that was deliberately not taken. Open question 25 has the whole argument,
  including what a *resume* feature would be instead — one invisible snapshot
  per game, Xbox Quick Resume's shape, which is a different feature and is not
  queued.
- ~~**NOTHING IS IN CI OR IN THE IMAGE.**~~ **IT IS IN BOTH, 2026-09-21, AND
  THE IMAGE BUILD ASSERTS ALL FOUR PIECES.** `.github/workflows/build-pcsx2.yml`
  builds it at the pinned v2.8.2 and uploads it in the same three-folder shape
  every other payload job uses; `ci/stage-image-payload.sh` **refuses to build
  an image without it**. Proved by running, not by reading the workflow:

  ```
  cores     21 in 287M
  ps2       28M emulator, 2 libraries, 9.5M resources
  [cabinetos] ok: the PlayStation 2 emulator (/usr/lib/cabinetos/cores/cabinetos-ps2.so)
  [cabinetos] ok: PCSX2's rapidyaml / c4core / resources
  ```

  **AND IT HAS BEEN BOOTED AND PLAYED, 2026-09-21.** That happened on
  `sha256:ce25da80…`, which is now the ROLLBACK deployment — the machine has
  since moved to `sha256:ceafc2bb…`. `20-heavy-systems.conf` is DELETED and the
  session runs `/usr/bin/cabinetos-frontend` with **no drop-ins at all**:

  ```
  [ps2] VM starting
  [ps2] game: "Homura" serial=SLES-53964 crc=69E02692
  [ps2] renderer Vulkan, upscale 1x, anisotropy 0
  ```

  **So the instruction at the top of this file is reversed: the machine runs the
  image and nothing by hand.** `ps -eo args | grep cabinetos-frontend` should
  show `/usr/bin/cabinetos-frontend` and nothing from `/var/home`. If it shows
  otherwise, somebody put a drop-in back.

  **IT RENDERS AT 1x NOW, AND THAT IS NOT A REGRESSION.** `--ps2-upscale 4
  --ps2-aniso 16` lived in the deleted drop-in, never in the image, and the
  defaults are 1.0 and 0. A PlayStation 2 at 640x448 on a 4K panel looks
  markedly worse than what MMagTech had been playing. **This is open question 23
  arriving with a face on it** — those flags were always a test instrument and
  nothing yet exposes quality as a real setting. It is now a concrete decision
  to be made against a television rather than an abstract one.
- ~~**The emulator carries two libraries the image lacks.**~~ Handled.
  `frontend/ps2/compile.sh` already linked with `-Wl,-rpath,$ORIGIN
  -Wl,--disable-new-dtags` — **RPATH and not the modern RUNPATH**, because
  RUNPATH is not inherited and libryml was found and then could not find
  libc4core sitting in the same directory. **The manifest it writes was EMPTY
  on any rebuild**, though, because it skipped recording a library it had
  skipped copying — so anything staging from it would have shipped an emulator
  that cannot dlopen. Found on 2026-09-21 by looking at the file rather than
  trusting it; it is now written from what is actually beside the emulator.
- ~~**The two open bugs in item 1b have NOT been re-tested against this.**~~
  **BOTH ARE CLOSED, 2026-09-21.** The tunnel went with the move to upstream
  PCSX2 — it belonged to the deleted libretro core. The pause-menu exit hang has
  not recurred: MMagTech, *"you can consider the hang done as we havent hit it
  again so i think switching cores or some other work fixed it."*

  **RECORDED AS NOT REPRODUCED, NOT AS FIXED**, and the difference matters. No
  change was made that targeted it, so the suspect —`vk::destroyContext` calling
  `deviceWaitIdle` on a device the core created and may already have torn down —
  was never eliminated. It outlived a change rather than being killed by one. If
  it returns, that is still where to look, and the instruction stands: **leave
  the console stuck** and take `gdb -p <pid> -batch -ex "thread apply all bt 12"`.

#### THREE FAULTS FOUND BY PLAYING IT, AND ALL THREE WERE SILENT

Nothing in the headless harness caught any of them. Worth remembering next time
somebody is tempted to call a thing finished off a capture.

- **NO SOUND IN GAME, AND GARBAGE AFTER EXITING.** The audio drain asked for
  170 ms of samples on every frontend frame, about a thousand times a second.
  `ReadFrames` does not refuse — it pads with silence — so the buffer was
  emptied on the first call and returned silence ever after, and the real
  samples arrived at once when the stream was torn down. It now takes only what
  `GetBufferedFramesRelaxed` says is there.
- **THE PICTURE DID NOT REACH THE TOP OR BOTTOM OF THE SCREEN.** Integer
  scaling: a 640x448 frame floored to 2x draws 896 rows of 1080. Right for a
  Game Boy's pixel grid, wrong for a 3D machine — the console already knew that
  for Dreamcast and simply did not count PCSX2 as hardware-rendered.
  `Core::hardwareRendered()` says yes now, which is also true.
- **AND CHASING THAT FOUND A THIRD NOBODY HAD SEEN.** The picture was 7% too
  wide, because the frontend derived its shape from the pixel dimensions and a
  PlayStation 2's pixels are not square. PCSX2 now returns an
  already-correctly-shaped frame, which also makes widescreen games right.

**A FOURTH WAS MINE AND IS THE MOST EMBARRASSING.** `--ps2-upscale 4` did
nothing for an hour, because the frontend on the console was a STALE BUILD that
did not know the flag and ignored it silently. It was reported as working off
the command line I had typed rather than off anything the machine said.

**That is why `[ps2] renderer ... upscale ... anisotropy ...` now prints on
every launch, from `GSConfig`** — the APPLIED configuration, not the requested
one. A setting that does not take shows up as a wrong number rather than as a
picture somebody has to squint at. **And check the binary's checksum against
the one you built before believing a deploy**; nothing does that automatically
yet.

### THE PLATFORM AUDIT, AND WHERE THE MISSING GAMES ARE

Done 2026-09-20 against the running console and Cabinet's own manifest. **36
platforms, 1650 games, 1147 playable.** CabinetOS ships exactly Cabinet's tvOS
core set — all 21, no gaps.

| | Games | |
|---|---|---|
| Libretro core exists, nobody added it | **73** | Jaguar 48, ColecoVision 25 |
| ~~Cabinet solved it on macOS, we have not~~ | ~~85~~ **0** | **PS2 71 and GameCube 14 are both in the image as of 2026-09-21.** PS2 has been played off it; GameCube has not — see below. |
| Nobody has solved it | **174** | Switch 109, PS3 32, Vita 27, Xbox 4, Wii 2 |
| **Will never be built** | 171 | Game & Watch — *"too small on a tv"* |

**THE TOTALS IN THIS FILE DISAGREE WITH EACH OTHER AND NOBODY HAS RECOUNTED.**
This audit says **1650** games; *Where things stand* says **1147 of 1644**; a
first-run note says "sixteen hundred". The 1147 and the 1232 are consistent
everywhere and are the numbers that matter, but the library total is carried
rather than measured and one of these is wrong. **Recount it against the server
before quoting a total anywhere it matters** — it is one query, and this file
has been quoting all three for days.

**GAMECUBE IS PINNED AND IN THE IMAGE, 2026-09-21 — AND HAS NOT BEEN PLAYED OFF
IT.** `dolphin` at `1a0f97270b70`, merged as #43. The pin was chosen by reading
the revision out of the hand-built `.so` that had been sitting in a home
directory on the A9, which is the same unshippable state PlayStation 2 was in
that morning. It needed no frontend change — `catalog.cpp` has routed
`ngc -> dolphin` since the table was written.

**`-DENABLE_X11=OFF` and it is correct rather than a workaround.** Upstream
defaults it ON and then requires `xi>=1.5.0`, which failed the configure in a
container with no X11 headers. Every "X11" in `Source/Core/DolphinLibretro` is
**DX11**, Direct3D 11 — checked, not assumed. What upstream means by X11 support
is Dolphin's own desktop windowing, which a libretro core never reaches.

**PLAYED OFF THE IMAGE, 2026-09-21**, on `sha256:ceafc2bb…` with no drop-ins:

```
[core] hardware rendering: Vulkan, bottom-left origin
[core] first hardware frame: 640x528 into the 0x0 target
[core] loaded .../930 - Ikaruga/Ikaruga.rvz
[core] 640x528, 59.9401 fps, 32029 Hz, aspect 1.3333
[launch] running dolphin-emu
```

**Vulkan is why this core works at all and nothing in the build selects it:**
Dolphin renders from a thread of its own, which a GLES context cannot serve. The
core asks through `GET_PREFERRED_HW_RENDER` and the frontend answers — the line
above is the frontend saying what it actually gave, not what was asked for.

**ONE THING NOBODY HAS EXPLAINED.** The CI-built core is **28.4 MB** and the
hand-built one that had been sitting in `~/cores-dev` was **16.5 MB**, at the
same revision. Both run. The likeliest answer is debug symbols — nothing strips
it and `CMAKE_BUILD_TYPE=Release` does not — but that is a guess and it has not
been checked. Worth a minute before anyone worries about image size.

**THE HEAVY SYSTEMS ARE COMING.** MMagTech, 2026-09-20: *"switch, ps3 and xbox
will be brought to the OS because we have less constraints to work with in linux
and more power. Same with Wii U if I get more games."* That settles a question
this project had only ever discussed as a recommendation. **PS2 and GameCube
first; Wii last and probably free, because Dolphin does both.** Open question
12b has the reasoning.

### 4. The console dies if the server is away — **the machine half is FIXED**

**It happened on this session's first boot**, which is how it stopped being a
story about one cold boot: upgrade the A9, reboot, and there was a real chance
the machine you came back to was on llvmpipe telling you it had no GPU.

```
[romm] nothing answered at 192.168.1.10:6005 over http or https
[gamescope] launch: Primary child shut down!
cabinetos-session: gamescope (drm) died on startup
cabinetos-session: WARNING — no Vulkan-capable GPU. Falling back to cage.
```

**That message was false and the same log disproved it four seconds earlier**:
`vulkan: selecting physical device 'AMD Radeon 890M Graphics (RADV STRIX1)'`
and `drm: selecting mode 3840x2160@60Hz`.

**Both machine-level faults are fixed, 2026-09-19, and both were measured on
the A9 against the real failure** — see PROJECT.md, open question 22:

- **The ladder asks instead of guessing.** Its old test was "is the process
  alive five seconds later", attributed to the compositor, and gamescope exits
  with its child — so an app quitting at one second looked exactly like a GPU
  that cannot do Vulkan. It now uses gamescope's `--ready-fd`, with a Wayland
  socket appearing as the backstop for cage. **Once a compositor is up, a dead
  child is the app's exit and never a reason to fall down the ladder.**
- **The frontend waits ninety seconds for the server** rather than exiting at
  once, says so, and says when it answers.
- **Ordering after `network-online.target` was considered and rejected**,
  because it delays the picture on a console with no network in exchange for a
  race the retry already closes. A decision, not an omission.

**WHAT IS STILL OWED IS THE PRODUCT HALF, AND IT IS THE BIGGER ONE.** All of
this makes the machine recover; none of it makes an offline console useful.
Open question 22 has the design with Cabinet's own rules quoted: **kept games
play with no server** (the library deliberately does not), a keep has to
**save the cover and a record** because our layout recovers the id and name but
not the art, saves **write to disk first and upload later** with a four-rule
precedence at launch, and **offline the console stays as the last user it knew**
and offers no switcher it cannot honour — MMagTech's call, 2026-09-19.

**AND FIRST RUN CANNOT BE COMPLETED WITHOUT A NETWORK.** MMagTech,
2026-09-19: the entirety of this OS relies on a RomM server, so one of Ethernet
or Wi-Fi must be working before setup can proceed — there is no "continue
without a network". **Wi-Fi is offered even when Ethernet is already up**,
skippable in that case and required otherwise, because it is the fallback for
the cable being unplugged and first run is the one moment it can be set up with
a keyboard to hand. This REVERSES open question 17's old rung 1, which skipped
the Wi-Fi screen entirely whenever a cable was live.

**But it is NOT a first-run branch, and that was decided 2026-09-19.**
MMagTech: *you cannot have kept games until a server has been paired and you
have kept one.* So first run assumes a server and is a linear path to pairing
one, with no skip — and the offline console is a strictly LATER state, a
machine that HAS been paired and now cannot reach its server. It may therefore
assume it knows the user, the library it last saw and which games are kept.
**"No server yet" and "no server right now" are different problems and only the
second has anything to work with.** See open question 15b.

**The stand-in demo library must never appear on a console.** It is today's
fallback when no address is configured at all, and it is worse than an error:
it looks like a working console showing somebody else's games.

### WHAT THE FRESH INSTALL IS STILL FOR

Everything here has been walked on the reference console — but that machine is
CONFIGURED. The fresh install is the first time the whole chain happens for real
end to end, and the only way to see these together:

| | |
|---|---|
| **The writes** | `config/first-run.json`, `config/server.json` and the token, written by the flow rather than by hand |
| **An empty Bluetooth list** | Every run so far had the Pro Controller already paired and trusted, so the list was never empty and the pad never had to be *discovered*. **This is what every real first run hits and it has never been exercised.** |
| **An unknown server** | The address has always come from `session.env`, so the server step has never been reached with nothing in it — the one field somebody actually has to type |

**A Bluetooth oddity was seen on 2026-09-20 and deliberately dropped.** MMagTech
saw something wrong on the controller step and judged it to be the pads already
being paired and known to the OS, which a fresh install will not be. Rather than
chase a theory on a machine that cannot reproduce the honest case, **look for it
again on the fresh install** — and if it is gone, it was the stale state.

**What to check while you are there**, because a fresh install is expensive and
nobody wants to do it twice:

- The three files above actually appear, and a REBOOT goes straight to Home
  rather than back into setup
- The Bluetooth list with nothing paired: does a pad in pairing mode appear, and
  does picking it pair, trust and connect
- Typing a server address into an empty field, with a keyboard and with a pad
- The QR on the television, scanned with a phone, approved for real

**What is still owed:**

- **DONE 2026-09-20: joining a real network, and walking the whole flow.** The
  reference console had its saved Wi-Fi deleted and its cable pulled — genuinely
  offline — and was set up from the screen alone: joined in about thirty seconds
  including typing the password, `MMagTech.nmconnection` written root-owned 0600
  with autoconnect on, running on the radio with both Ethernet devices
  reporting `unavailable`. **That is the case the hard gate exists for.**
- **Small UI tweaks.** MMagTech's words, 2026-09-20: *"might be some small ui
  tweaks later but functionally great."* Nothing is blocked on them.

**How to see any of it:**

```
--setup      --setup-step <name>       --no-setup
--first-run  --first-run-check-server  --first-run-step <name>  --first-run-rules
--network    --network-scan            --qr "<text>"            --qr-out <path>
```

The probes all run before SDL and none of them disturbs the session on the
television.

### 5. Where the in-game save machinery lives

Item 3 is the job; this is the map. `frontend/src/filesave.{h,cpp}` is the
mechanism and `catalog::saveFiles` is the table. The two rules that are not
obvious and were both paid for:

- **Restore before the core loads the game.** These cores read their save file
  once, synchronously, while the machine is being built. A file that arrives
  afterwards is a file the game has already decided is not there.
- **Capture after `retro_unload_game`.** A core buffers its writes and flushes
  at shutdown, and Flycast only closes the VMU in its device's destructor.

### 7. Finish the core options, which is half done

The host answers every option a core declares, and the override table is wired
into the launch path as well as the audit. Two things are left:

- **Bring across Cabinet's per-platform choices.** `catalog::optionOverrides`
  has three entries: PPSSPP's CPU engine, Genesis Plus GX's `system_bram`, and
  Opera's `bios` and `nvram_storage`. The last two were added by the save work
  because **the save PATHS depend on them** — and `opera_bios` turned out to be
  the difference between 3DO booting and 3DO not starting at all, which nothing
  had noticed because nobody had run a 3DO game. **That is the argument for
  doing the rest**: an unanswered option is not the default, and here it was
  silently deciding where a person's save lived. Cabinet hand-picks a subset
  per platform in `NativeCoreOptions.swift`; port it one platform at a time
  with a reason recorded beside each choice, and **run a game on each platform
  afterwards** rather than trusting the table.
- **The options MAME asks for and never declares.** Two are constant across
  every game tried and the rest vary by driver. Their values have to come from
  the core's source, not from a guess.

### 8. The N64 save states that do not restore exactly

Reproducible to the digit, the instrument was checked, and three candidate
causes are written down with none established. It blocks nothing today, and it
matters because portable save states are the premise the CARTRIDGE-ERA half of
this product rests on — **bounded by open question 25 on 2026-09-21**, which
removed them from PS2, GameCube and everything after. N64 is on the side where
they stay, so this is still worth fixing.
The cheapest discriminating experiment is in PROJECT.md.

### 9. PSP's save state, and a crash that is understood but not closed

**The save DATA is done.** Two things are left, and they are the same shape:
PPSSPP is the only core that emulates on a thread of its own.

**A threaded core only advances when the frontend COMPLETES a frame**, not when
`retro_run` is called. That one fact explains both of the following.

**The crash.** Quitting a PSP game while it is still booting used to kill the
console. Quitting now defers until the machine is up — measured on the case
that crashed twice, which waits 4.1s and exits cleanly. **It is not closed:** in
the headless capture configuration the core sometimes never boots at all (one
run: 2,384 frames, zero audio), and tearing it down then aborts at process exit
in a static `std::thread` destructor inside the core.

**The state.** PPSSPP produces a 41,943,040-byte state at a demo screen, so it
CAN serialize. Whether the restore is exact is unknown, because `--state-test`
warms up in a tight `retro_run` loop with no frame in it and this core makes no
progress there. **Fixing the instrument is the work**, and the diagnosis above
is the fix: give the warm-up a real frame loop.

### 9b. GameCube audio is wrong — REPORTED 2026-09-21, NOT INVESTIGATED

MMagTech, playing on the A9 during the UI session: *"gamecube audio is messed
up."* Recorded here and deliberately not chased, because it arrived in the
middle of a UI pass and guessing at it would have cost that session.

**Nothing is known beyond the sentence.** Not which game, not how it is wrong —
crackling, wrong pitch, stuttering, missing channels and desynced-from-video are
five different faults with five different causes, and the first job is to find
out which one this is. Ask before reading any code.

Where to start when somebody does:

- **The core is Dolphin's**, and GameCube arrived on the A9 on 2026-09-20 along
  with PlayStation 2 — see the Vulkan notes. Both were about getting a PICTURE
  on the screen; nobody listened to either of them carefully.
- **The frontend's audio path is one place**, `SDL_OpenAudioDeviceStream` at the
  core's declared sample rate with the frame loop pushing. A core whose real
  output rate differs from `av_info.timing.sample_rate` produces exactly the
  family of faults above, and Dolphin's is 32000 or 48000 depending on the
  game and on its own settings.
- **Compare against Dreamcast**, which is the other heavy hardware-rendered core
  and is known good. If Dreamcast is clean on the same television and the same
  session, the fault is Dolphin's configuration and not the console's output.
- **The UI sounds now share that device** — `frontend/src/sound.cpp` opens its
  own stream at 48000. It was added on 2026-09-21, AFTER this was reported, so
  it cannot be the cause; but it is a second stream on one device and is worth
  ruling out with `--ui-sound off` before blaming the core.

### 9d. DREAMCAST WILL NOT LAUNCH ON THE A9 — FOUND 2026-09-21, HALF FIXED

MMagTech: *"dreamcast game downloaded and didnt auto launch and clicking play
didnt launch it either."* The console knew exactly why. From the journal:

```
[launch] Zero Gunner 2 (Dreamcast) via flycast
[launch] already downloaded, 356293045 bytes
[launch] this core wants Vulkan and this device will not export memory as a
         file descriptor, so nothing it draws could reach the screen
```

**THE UI HALF IS FIXED.** A refusal now reaches the launch screen through
`setNotice`, and a press from somewhere with nowhere to put a message — Home's
Resume card — opens the game's own screen to say it. A refusal the person cannot
see is indistinguishable from a console that has stopped responding, and it is
worse than a crash: pressing the button again does the same nothing forever.

**THE REAL HALF IS OPEN AND IT IS NOT A UI PROBLEM.** `vkhost.cpp` requires
`vkGetMemoryFdKHR` and the A9's driver is not exporting it, so the Vulkan path
cannot hand Flycast's picture to the GL context. Things worth knowing before
somebody starts:

- **This is the `vulkan-host` work**, the same machinery PlayStation 2 and
  GameCube arrived on. Whether THEY still launch on this device is the first
  question — if they do, the extension is present and Flycast is asking for it
  in a way the others do not.
- **Dreamcast played before.** "Dreamcast, Naomi, N64 and PSP play, through a
  framebuffer inside the frontend's own GLES context" is recorded in this file
  as done, and one of the platform sweeps launched a Dreamcast game and read its
  pixels. So this is a REGRESSION or a device/driver change, not a thing that
  never worked — find out which before redesigning anything.
- **N64 still launches**: the same journal shows `1080° Snowboarding` running on
  mupen64plus immediately afterwards. So it is Flycast's path and not all of
  hardware rendering.

### 9e. N64 TEXTURES ARE WRONG — and they are wrong on CABINET too

MMagTech, 2026-09-21: *"same issue as other n64 on cabinet some textures arent
rendering correctly."*

**THE SECOND HALF OF THAT SENTENCE IS THE WHOLE LEAD.** Cabinet is the tvOS app,
a completely different frontend on completely different hardware with a
different graphics API, and it shows the same fault with the same core. That
rules out almost everything CabinetOS owns — the GLES context, the framebuffer
path, the texture upload, gamescope — because none of it exists on the other
side. What both have in common is mupen64plus-next and the options it is given.

So start at **item 7, the core options**, which is recorded there as half done.
`mupen64plus-next` carries a large options surface and several of them decide
exactly this: which RDP/RSP plugin is used, the texture filtering and
enhancement settings, and whether the high-level emulation path is taken at all.
An option left unanswered is NOT the core's default — that is the fault the
options work already exists to fix, and it is the most likely cause here.

**What is NOT known:** which games, and what "wrong" looks like — missing,
stretched, wrong colours, flickering and low-resolution are five different
faults. Ask before reading any code. Also worth knowing whether it is every N64
game or some, because per-game is a different problem from per-platform.

### 9f. NES AUDIO IS OUT OF SYNC — reported 2026-09-21

MMagTech: *"just noticed audio isnt syned on nes."*

**Two faults wear this description and they are not related**, so establish which
one it is before touching anything:

- **Audio LATE or EARLY against the picture, steadily.** That is a buffering
  problem and it lives in this frontend: `SDL_OpenAudioDeviceStream` at the
  core's declared rate with the frame loop pushing, and nothing anywhere
  measures or bounds the queue depth. A queue that grows by a few samples a
  second is inaudible for a minute and half a second behind after ten.
- **Audio DRIFTING further out the longer it runs.** That is a rate mismatch —
  the core produces samples at its own clock and the device consumes at the
  panel's 60 Hz, and the two are not the same 60. Every emulator frontend solves
  this with dynamic rate control, and this one has none.

**`SDL_GetAudioStreamQueued` answers which**, and it is already used in
`sound.cpp` for a different reason. Log it once a second during a game: flat
means fault one, climbing means fault two.

**RELATED, AND WORTH DOING FIRST:** GameCube audio is item 9b and was reported
the same day. If both are the same shape, it is the frontend's audio path and
not two cores — which would be good news, because it is one fix.

### 10. Nothing warns that a system's BIOS is missing

Until a game fails to start. `catalog` is where it belongs — a fifth answer, and
the first one that is a fact about the person's server rather than about this
console. The answer is a lookup, not a layout, so the tile that shows it can
reuse the wording already measured for the other four.

### 11. The disk that eviction cannot see

Mesa's shader cache in `~/.cache`, plus files the cores write into `bios/`.
Under 3 MB today. One of them is a Dreamcast's saved clock and language
settings, so "clean the system directory" is not the answer — though it is the
cheapest of them to lose, because it rebuilds itself.

**This shrank again on 2026-09-19.** PPSSPP's 13 MB are out of `bios/` on any
machine built from the image: they ship at `/usr/share/cabinetos/system/` and
`ensureTree` symlinks them in. **On the test VM they are still real files in
`bios/PPSSPP/`**, where the core build put them, and the link step correctly
leaves them alone — so the VM and a console differ here, on purpose.

### 12. Power button to a clean shutdown

Phase 2's last mechanical item, and it is a behaviour rather than a picture.
Phase 2's other leftover is the **boot splash**, which is a picture and waits.

---

## Available now that the console runs on a television

**The wait is over** — the A9 runs the frontend on its own GPU at 3840x2160.
These are ordered. **Do not begin any of them in the VM.**

- **The navigation bar.** The Library is reached with a temporary **L** key.
  Home has about 85 points of vertical slack and the bar needs about 85 — the
  arithmetic is in PROJECT.md. Either the bar fits, or the hero comes down, or
  the bar goes elsewhere, and only a television can say which.
- **The Storage screen.** Its data already exists and can be finished without
  it — run `./build/cabinetos-frontend --storage` — but the screen is a layout.
- **The rest of the launch screen**: a different save state, a different core,
  an export. **The save-state half is a mechanism and can be built now.**
- **Download All, at the platform level.** Cabinet's `DownloadAll.swift` sizes
  the whole list and refuses rather than filling the disk. The sizing and the
  refusal are measurable; the screen is not.
- **PSP's internal resolution.** The core is answered with its own declared
  default, 480x272, which is the PSP's own screen and what Cabinet ships on a
  television. Cabinet's Mac uses 1920x1088. Raising it is a look-and-performance
  decision and it needs the panel.
- **The audio governor's 20 ms cushion.** Inherited from Cabinet rather than
  measured here; the lead it permits *is* input lag. Tune it with a pad in hand.
- **First run's SCREENS.** Its four mechanisms were built on 2026-09-20 and none
  of them is a picture; see item 4b. What waits is every screen. The old text
  follows, because the design behind those screens is unchanged — **open
  question 15b**,
  written with MMagTech on 2026-09-19 after setting the A9 up by hand over
  SSH. The requirement is one line and it is testable: **a keyboard is needed
  exactly once, ever.** A keyboard is the only input an installed machine
  guarantees, because the firmware boot menu needs one; a wired controller is
  not, because most pads sold now are Bluetooth. So setup runs on a keyboard,
  pairs a controller as its last step, and a second controller is added using
  only the first — with two-sided confirmation, so a neighbour's pad in
  pairing mode cannot answer for itself. **The mechanisms mostly exist** (the
  on-screen keyboard, the pairing flow's code and URL, `session.env`, bluez),
  **and as of 2026-09-20 so do the four that did not** — the state machine, the
  QR renderer, the NetworkManager plumbing and knowing it is the first run.
  What is left here is the look.
- **The boot splash**, and the rest of the branding.
- **The row in Settings that turns file access on**, decided 2026-09-19 and the
  answer to open question 9. A console ships listening to nothing; an ordinary
  visible row turns SFTP on and shows the address, the user name and a password
  the machine generated for itself. Not the hidden developer-mode toggle this
  project planned for two weeks — that machinery exists to conceal something
  dangerous and reaching your own saves is a feature.
- **Everything about motion, the letterbox glow and the safe area.**

## Things that will bite you

### About the image, which is new territory

- **`/var` IN A BOOTC IMAGE IS UNPACKED FROM THE FIRST IMAGE ONLY.** Anything a
  build writes there lands on machines installed after it and **never** on
  machines that upgraded into it. The build is green throughout. This bit open
  question 21 (flatpaks) and it nearly bit the storage root — which is why
  `/var/lib/cabinetos` is created every boot by
  `system_files/usr/lib/tmpfiles.d/cabinetos.conf` rather than by a `mkdir`.
  **Everything the image installs goes in `/usr`.**
- **Without that directory the console writes to `/`.** `storage::root()` tries
  `/var/lib/cabinetos`, and the session user cannot create it, so it falls back
  to the working directory — which for a systemd service is the root of the
  filesystem. `WorkingDirectory=/var/home/cabinet` in the unit makes the
  fallback visible rather than catastrophic, but the tmpfiles rule is the fix.
- **`ldd` prints "not found" and exits 0.** `build_files/install-frontend.sh`
  reads its output, not its status, for the frontend and all twenty-one cores.
  **Run the control** — the same script into a bare `fedora:44` names all six
  libraries the frontend needs, plus `libGL` for melonDS and **`libX11` and
  `libXext` for PPSSPP**, which nothing had written down anywhere.
- **`mesa-libGLES` is not in the base image, and `libGLESv2.so.2` is there
  anyway** — `libglvnd-gles` provides it. Do not "fix" a missing package that
  is not missing.
- **An artifact's internal root is the least common ancestor of the files it
  actually found.** `build-core.yml` uploaded `cores/build/*.so` and
  `cores/system/`, so the twenty cores with no system files got an artifact
  rooted one directory deeper than PPSSPP's. Two layouts under one naming
  scheme, invisible until something downloaded them. Both jobs now stage into a
  fixed `artifact/` directory and upload that.
- **`just build` and `just generate-build-tags` decide whether to stamp the
  image by asking whether `git status -s` is empty.** An untracked directory in
  the working tree silently costs the image its labels and its git-sha tags.
  `image_payload/` and `.artifacts/` are gitignored for that reason, not for
  tidiness.
- **A called workflow's concurrency group can collide with its caller's**, and
  `cancel-in-progress` then has a run cancelling itself — a red build with no
  failing step. `build-core.yml` and `build-frontend.yml` therefore have **no**
  `concurrency:` at all, and `build.yml`'s group is a literal prefix rather
  than `${{ github.workflow }}`. If that ever needs to change, the fix is a
  `workflow_call` input used in the group string — but test it, because an
  invalid `concurrency` expression is a workflow that does not parse.
- **A workflow that build.yml CALLS must not also trigger itself where
  build.yml runs**, or one change queues the same twenty-one core builds
  twice. That is not merely wasteful: the runner concurrency cap means the
  duplicate starves the image build of the runners it is waiting for, and it
  doubled the wall clock of the change that introduced it. Cancelling one by
  hand leaves a red cross on a pull request whose code is fine, which is how a
  check stops meaning anything. So both called workflows now use
  **`pull_request: branches-ignore: [main]`** and have no `push:` trigger at
  all — build.yml covers main in both directions, and its paths-ignore is
  documentation-only so it can never skip a change under `cores/`.
  **Do not narrow them to `branches: [main]`**: that is the 2026-09-16 hole
  where a stack of branches slipped past every check.
- **The image build is now about twenty-three minutes**, not thirteen: it
  builds the cores first. That is the honest price of the image containing what
  it claims to, and it buys an image build that proves all twenty-one pins. If
  it becomes a problem, cache `cores/build` on the hash of `cores/build-core.sh`
  — but keep the revision assertion running on a cache hit, or the check that
  justifies the whole workflow stops happening.

### About real hardware, learned in one hour of it

- **The installer is not fit for anyone but us**, and open question 5 now says
  so with the detail. It brands itself Bazzite, its media check FAILS on good
  media (`Supported ISO: no`, aborting at 4.8% — the write was byte-exact and
  the install from it worked), it scrolls `amdgpu: Fatal error during GPU
  init`, and it asks about UIDs. Installing works; the experience does not
  ship.
- **The installer runtime carries NO firmware**, which is why the GPU and
  Wi-Fi die in it and why neither matters. The tell: it also failed to load
  `gc_11_5_0_pfp.bin`, a file that *is* in Fedora's package and *is* in our
  image. **One thing that should not have failed was worth more than all the
  things that did.** Its kernel is stock Fedora's, not ours.
- **DO NOT DIAGNOSE HARDWARE FROM A PHOTOGRAPH.** Two theories were built and
  discarded here — one from a filename nobody checked, one from a digit
  misread off a picture of a rotated monitor. Get a shell and read `dmesg`.
- **`journalctl -u cabinetos-session` shows almost nothing.** The script's own
  output carries the syslog identifier, so the unit filter returns only
  systemd's start/stop lines. Use **`journalctl -t cabinetos-session`**. And
  the clock jumps when NTP syncs after an install, so `--since` lies on the
  first boot; use `-n`.
- **gamescope does not pick the display's mode on its own.** With no
  `--output-width`/`--output-height` at all it still chose 1920x1080 on a
  3840x2160 panel. If you want native, read the connector and pass it.
- **A 4K panel is the default assumption now, not a possibility.** PROJECT.md
  always said most sets are 4K; the first one plugged in was.

### About looking at what you built

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows. **Vertical fit cannot be judged here either.**
- **Read the pixels before believing the picture.** PPSSPP's first capture was
  not blank — it was a plausible, nearly-black rendering with faintly legible
  text. The maximum pixel in the whole 1920x1080 frame was RGB **(4,4,4)**.
- **Every screen photographs itself, headless.** `SDL_VIDEODRIVER=offscreen`
  needs no compositor, no session and no controller:
  ```
  SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --romm 192.168.1.10:6005 \
    --screen library --screenshot /tmp/x.bmp --render-size 1920x1080 --frames 60
  ```
  `--screen` opens by walking the route a person walks, so a capture cannot show
  a state the product cannot reach. `--storage`, `--download` and `--unkeep` do
  the same for the things with no picture.
- **`--frames N` only ends the run when there is a `--screenshot` to take.**
  Without one the loop never exits and the command sits there at full CPU.
- **`--launch-after` IS WALL-CLOCK SECONDS AND THE UI LOOP IS NOT PACED**, so
  on the A9's Radeon a run of 1600 frames goes by in under a second and the
  launch never fires at all — no error, no `[launch]` line, just a screenshot
  of Home. It worked on the VM the whole time because llvmpipe is slow enough
  to take longer than a second. **Use `--launch-after 0`.** Twenty minutes went
  on this, chasing a difference between two binaries that did not exist.
- **The same capture command is worth running ON THE A9**, with
  `SDL_VIDEODRIVER=offscreen` and `--render-size 3840x2160`. It uses the real
  Radeon, needs no compositor, and does not disturb the running session — so a
  4K frame off the reference GPU costs one command and no downtime.
- **`--storage-root <path>` puts a whole console somewhere else**, which is how
  the two-disk and the out-of-space tests were run without disturbing the real
  tree.
- **`$CABINETOS_ROMM` works everywhere `--romm` does**, which is often shorter.
- **To watch a real game, launch it**: `--launch <romId> --launch-after 1
  --frames N`. A PSP game needs about 2500 drawn frames to reach its attract
  demo on this VM; Dreamcast about 1400. Add `--overlay-exit` to make it quit
  back to Home by itself, which is the only way to exercise the unload path
  without a controller.
- **Stop the session before building on the VM.** The frontend runs at 300% CPU
  under llvmpipe and it is four cores. Or skip it and use the offscreen driver.

### About the network, polkit and QR codes, all new on 2026-09-20

- **A COMMAND THE FRONTEND *RUNS* IS INVISIBLE TO EVERY CHECK THIS REPO HAS.**
  `ci/base-watch.txt` watches shared LIBRARIES and `require-frontend-libs.sh`
  reads `ldd`, so a strip pass that removed NetworkManager or polkit would leave
  a green build, a binary that links perfectly, and a console that cannot see a
  Wi-Fi network or say why. `net.cpp` needs `/usr/bin/nmcli` and
  `/usr/bin/pkcheck`; `build.sh` now asserts both. **Anything else that shells
  out needs the same treatment.**
- **`pkcheck` PRINTS SEVERAL `key=value` LINES, NOT ONE.** Taking the last `=`
  in its output reports `1` — the value of
  `polkit\56retains_authorization_after_challenge`, which is not even one of the
  values the action can have. Match the line whose key ends in `result`.
- **THE POLKIT ANSWER DEPENDS ON WHO IS ASKING, AND BOTH ANSWERS ARE RIGHT.**
  Over SSH the verdict for saving a network is `auth_admin_keep`; from the
  console's own session it is `yes`. The rule requires `subject.local`, on
  purpose — developer mode hands out SSH deliberately and a shell over the
  network should not inherit the console's privileges. **`--network` says which
  question it put**, because a probe that printed one number without saying
  would be the third lying instrument this project has fixed.
- **A POLKIT RULE THAT GRANTS SOMETHING ALREADY GRANTED PROVES NOTHING.** The
  machine already answers `yes` via `wheel`, so installing a rule that also says
  yes changes nothing observable. **The decisive test is to install it returning
  `NO` and watch the verdict flip** — that proves yours is consulted first.
  `60-` sorts before `org.freedesktop.NetworkManager.rules`, and polkit takes
  the first rule that returns a result. Put the machine back afterwards.
- **`nmcli --terse` ESCAPES COLONS, AND AN SSID MAY CONTAIN ONE.** Anybody
  within radio range picks their own SSID, so a naive `split(':')` is a stranger
  deciding how many fields this console thinks it received. And **nothing may go
  through a shell**: every nmcli call is `fork`/`execvp` with an argv array,
  because a scan puts unvetted bytes from strangers into this process every time
  it runs.
- **ONE ROW PER NETWORK, NOT ONE PER ACCESS POINT.** A house with three mesh
  nodes broadcasts the same SSID three times and nmcli lists all three.
- **`/etc/cabinetos/session.env` IS NOT IN YOUR ENVIRONMENT OVER SSH.** The
  session script sources it; an SSH shell does not. Every check of first run is
  made over SSH, so `--first-run` reported "NEEDED" on a console that had been
  working for a day. The frontend now reads the file directly as well.
- **A QR CODE CANNOT BE CHECKED BY LOOKING AT IT** — a wrong one looks exactly
  like a right one. Check it three ways: module-for-module against a reference
  **with the mask forced**, a second reference to break ties, and a round trip
  through a real decoder. Each of the three found something the others did not.
- **THE MASK IS A LEGITIMATE DIFFERENCE BETWEEN ENCODERS.** Three
  implementations picked three different masks for the same string and all three
  are valid. So "matches a reference exactly" is not achievable across
  implementations — force the mask to compare the rest, and decode to settle it.
- **IN A BCH REMAINDER, TEST THE GENERATOR'S DEGREE, NOT THE FIELD'S WIDTH.**
  Testing bit 14 instead of bit 10 reduces nothing and puts **no error
  correction at all** in the format field. The data region was perfect and no
  scanner would read it.
- **THE QUIET ZONE IS NOT OPTIONAL AND IT IS THE RENDERER'S.** Measured: the
  same code drawn flush to the edge does not decode at all; with four modules of
  margin it decodes every time. It is the commonest reason a correct code will
  not scan.
- **`(6,8)` AND `(8,6)` ARE TIMING, NOT FORMAT.** They sit inside the format
  area's L shape and belong to the timing pattern. Reserving them blanks two
  modules of the timing line — a two-module difference in a 33x33 symbol that no
  scanner will accept.
- **`--romm-pair` DOES NOTHING IF A TOKEN ALREADY EXISTS.** It loads
  `$HOME/.config/cabinetos/romm.json` and, finding one, skips straight to
  reporting the library — so on either machine here it never pairs. To get a
  real pairing code without disturbing anything, point HOME somewhere empty:
  `HOME=/tmp/pairhome ./build/cabinetos-frontend --romm <addr> --romm-pair`.
  The token lands in the throwaway directory and nothing else changes.
- **THE PAIRING URL IS THE SERVER'S, NOT A SHAPE YOU CAN GUESS.** It is
  `base + verification_path_complete` from `/api/auth/device/init`, and on RomM
  5.1.0 that is `/pair/device?user_code=…`. A fabricated one produces a QR that
  scans perfectly and lands on a page saying the code does not exist.
- **ANYTHING FROM OUTSIDE THIS PROCESS IS A SNAPSHOT WITH A COST, AND EVERY
  SCREEN SHOWING ONE OWES TWO ANSWERS: WHO REFRESHES IT, AND ON WHICH THREAD.**
  That one sentence covers seven bugs found in an hour of walking first run on
  the television. Neither the Wi-Fi list nor the Bluetooth list had an answer to
  the first — both went on reporting what was true a minute ago, and a deleted
  Wi-Fi profile left a row claiming to be connected AND saved, so pressing it
  tried to join with no password. Four calls had the wrong answer to the second,
  the worst being `bt::adapter()` inside `rebuild()`: **two subprocesses every
  two seconds, for ever, to choose the wording of one row.**
- **A BLOCKING CALL ON THE FRAME THREAD LOOKS EXACTLY LIKE A WORKING FRAME IN A
  SCREENSHOT.** `bt::known()` ran there after a successful pairing — one process
  to list devices and another PER DEVICE, twenty-odd on a real scan — so the
  console froze for seconds at the moment it had just said "Controller ready."
- **THE CONSOLE SHOWED A BLANK SCREEN ON EVERY BOOT AND NOBODY HAD NOTICED.**
  Reaching the server, adopting the user and pulling sixteen hundred games all
  happen before the frame loop exists — seconds normally, up to NINETY when the
  server is not up yet. It took somebody pressing "Start playing" and expecting
  something to happen. `setup::showWaiting` now draws a still frame naming the
  stage. Nobody watches a console boot with a stopwatch.
- **A `void` FUNCTION THAT ENDS A SESSION TELLS NOBODY IT DID.**
  `Keyboard::pressKey` handled its own "done" and "cancel" keys internally, so
  driving the on-screen keyboard with a CONTROLLER and pressing A on "done"
  closed the panel and threw away what had been typed. The physical keyboard's
  Return worked, which is exactly why it survived. Three call sites doing the
  same job is what let one of them go unwired.
- **A SCREEN THAT WAITS FOR SOMETHING MUST KEEP LOOKING, AND MUST FETCH WHAT IT
  NEEDS RATHER THAN WHAT ITS ENTRY POINT NEEDED.** First run's network step read
  the facts once on arrival and started its Wi-Fi scan the same way. Enter it on
  a cable, then unplug: the panel correctly switched to a Wi-Fi list and showed
  the empty one nobody had ever filled — "Nothing on the air", in a house with
  four networks — and plugging the cable back in changed nothing on screen. Both
  halves are now driven by what the screen NEEDS, on a two-second worker.
- **NetworkManager REFUSES `--rescan yes` while its own scan is running**, and
  that reads as an empty sky. Fall back to the cached list. And tell "we looked
  and there is nothing" apart from "we could not look" — only one means retry.
- **A GUARANTEE STATED UNCONDITIONALLY BY A FLOW THAT CAN BE SKIPPED IS A BUG.**
  First run's last screen said "you can unplug the keyboard" — the promise the
  whole design exists to make — while the controller step it follows is
  deliberately skippable. Somebody who skips it has exactly one input and was
  being told to unplug it. **Anything that can be skipped must have its
  consequence said on the step that offers the skip, and every later promise has
  to be conditional on what actually happened.**
- **AN OFFSCREEN CAPTURE DOES NOT PROVE A WINDOW EVER GETS A FRAME.**
  `Renderer::beginFrame` binds an offscreen SCENE target so panels can blur what
  is behind them, and **`presentScene()` is what puts it on the real
  framebuffer**. Miss that call and the loop runs perfectly at sixty frames a
  second presenting nothing — while every `--render-size` capture comes out
  correct, because `saveFrame` reads the offscreen target directly. The
  television showed white, gamescope's own screenshot came back entirely black,
  the process sat at 5% of a core, and nothing logged an error. **Anything that
  draws a screen must call `presentScene()`, and anything drawn after it lands
  on top of the scene rather than inside it.**
- **`gamescopectl` IS NOT ALWAYS ON `gamescope-1`.** The socket number is
  whichever the current instance took, and it changes when the session
  restarts. List `$XDG_RUNTIME_DIR` and use the one whose mtime matches the
  running gamescope; stale sockets from earlier instances sit there looking
  identical.
- **A SETUP SCREEN'S LOOP MUST BE PACED, AND `--frames` DEPENDS ON IT.** A page
  of static text left unpaced runs at thousands of frames a second on the A9,
  and four hundred frames went by before the server had answered — so the
  capture of the pairing screen came out with no code on it. **The same trap
  `--launch-after` fell into, one screen along.**
- **BLUEZ USES THE ADDRESS AS THE NAME when a device has not given one**, with
  dashes where the address has colons. An unnamed device does not have an empty
  name, it has a name that looks like one — and the controller list filled with
  SIXTEEN of the neighbours' beacons before anybody noticed.
- **THE COPY ASSUMES A COMPETENT ADULT.** MMagTech, 2026-09-20: *"if you have a
  RomM server and can install an OS I shouldn't need to tell you in depth how to
  pair a controller."* Every line says the CONSTRAINT — required or optional,
  and why only when the why is not obvious — and stops. Titles say what the step
  does, not hello.
- **NEVER LET FOCUS LAND ON A ROW THAT DOES NOTHING.** Every placeholder in the
  setup flow is disabled, so this is the common case. A focus rim on a row that
  ignores the button cannot be told apart from a crash.
- **DO NOT SAY A SERVER DID NOT ANSWER BEFORE ASKING IT.** Arriving at the
  server step with an address already in `session.env` is the common case, and
  the screen reported it unreachable before sending a packet. It needed a fact
  at the rules level, not a fix in the screen.
- **`bluetoothctl pair` WITHOUT `trust` LOOKS EXACTLY LIKE A BROKEN PAD.** bluez
  refuses the incoming connection every time the controller wakes, so the pad
  pairs perfectly once and then never reconnects. It reads as "it keeps
  disconnecting" and has nothing to do with pairing.
- **WALK EVERY COMBINATION RATHER THAN RE-READING THE RULES.** The state
  machine's exhaustive check is 96 cases, needs nothing, and found a deadlock
  the code read as correct. Assert the REFUSALS — the happy path is the part
  that already works.

### About Vulkan, the two heavy systems, and their saves — all new 2026-09-20

- **THE CONSOLE RUNS ON X11, NOT WAYLAND.** gamescope embeds an Xwayland
  server and SDL picks the `x11` driver, so the GL context is GLX and **there
  is no EGL display in the process at all**. A picture handed over as an
  EGLImage fails with `EGL_NOT_INITIALIZED` on the television while working
  perfectly under `SDL_VIDEODRIVER=offscreen`, where SDL does use EGL. It cost
  a PlayStation 2 game that played with sound and a black screen.
- **AN OFFSCREEN CAPTURE DOES NOT PROVE THE SESSION WORKS, AND THIS IS THE
  SECOND TIME.** The existing note about `presentScene()` is the same lesson
  one layer down. **Anything touching the display path has to be run under
  gamescope before it is believed** — put `--launch <id> --launch-after 0` in
  the session drop-in, which is how both of these were finally caught.
- **`Environment=` IN A SYSTEMD DROP-IN SPLITS ON WHITESPACE.** An unquoted
  `CABINETOS_APP=/path --core-dir x` sets the path and silently discards every
  argument, and the console comes up looking correct on the default core
  directory. Quote the whole value.
- **`sudo -S ... | tail -0` HIDES A FAILED SUDO.** Three restarts in a row did
  nothing and the journal kept showing the old process, because the output
  that would have said so was thrown away. Print the exit status.
- **A BINARY THAT IS RUNNING CANNOT BE OVERWRITTEN** — `cp` fails with "Text
  file busy" and the restart brings back the OLD build, which reads exactly
  like the fix not working. Stop the session, copy, start.
- **THE COPY BETWEEN VULKAN AND GL COSTS TWELVE MICROSECONDS.** Everything else
  in that path is the frontend waiting for the EMULATOR to finish drawing,
  because the core's work is queued ahead of ours. Both obvious optimisations
  — an optimally-tiled destination through `VK_EXT_image_drm_format_modifier`,
  and an exported semaphore instead of the fence — were reasoned about, one was
  BUILT and measured, and neither is worth anything. The tables are in
  `vkhost.cpp`'s `createShared`. **Do not rebuild either without a number that
  contradicts them.**
- **`dolphin_renderer` IS NOT AN API SELECTOR.** It takes "Hardware" and
  nothing else in a release build; setting it to "Vulkan" silently turns
  hardware rendering OFF. The API comes from what the frontend advertises in
  `GET_PREFERRED_HW_RENDER`.
- **DOLPHIN DECLARES ZERO CORE OPTIONS UNTIL A GAME IS LOADED**, so
  `--core-options` reports none for it. That is the suspicious case, not the
  clean one — the same shape as FBNeo and MAME.
- **DOLPHIN'S USER DIRECTORY IS UNDER THE SAVE DIRECTORY, NOT THE SYSTEM ONE.**
  `Boot.cpp` prefers `<saveDir>/User` when the frontend gives it a save
  directory, and only falls back to `<system>/dolphin-emu/User`. An hour went
  on a `Dolphin.ini` written in the second place and read from the first.
- **`pcsx2_shared_memory_cards` DEFAULTS TO ON** and puts every game's save in
  one `Mcd001.ps2` in the system directory — a card that belongs to no rom and
  therefore cannot be synced at all. **`pcsx2_analog_mode1` DEFAULTS TO OFF**,
  which is the DualShock's analogue mode disabled and reads as dead sticks.
  Both are in `catalog::optionOverrides` now.
- **ROMM MATCHES A SAVE ROW BY FILENAME ALONE, AND THE MAC'S SPELLING IS
  DIFFERENT FROM THIS CONSOLE'S.** `cabinet-604.ps2` against
  `Burnout 3 Takedown (Cabinet).srm`. Four separate things had to agree before
  one card could travel — the format, the name, the region extension and the
  emulator tag — and three of them were wrong. See open question 12b.
- **DOLPHIN PUTS THE REGION *AND THE CARD SIZE* IN THE FILENAME.** Ask for
  `cabinet-937.raw` and get `cabinet-937.USA.raw`, or `cabinet-937.USA.251.raw`
  for a 2 MB card. With the name goes the row's identity on the server, so
  `MemoryCardSize` is pinned here. **Cabinet for Mac leaves it at -1** and has
  the same latent fault — see the Cabinet-side debts.
- **A FRESHNESS RULE IS NOT OPTIONAL FOR A PLATFORM WHOSE CORE CREATES ITS OWN
  CARD.** Six of the seven PS2 and GameCube rows on the reference server held
  nothing: three PS2 cards were 8,650,752 bytes of `0xFF` with no format header
  at all, and three GameCube cards had nothing in either copy of their
  directory. Deleted 2026-09-20 with MMagTech's say-so, each re-verified empty
  immediately beforehand.

### About building a whole emulator rather than a core — new 2026-09-21

- **A LIBRARY THAT BUILDS TELLS YOU ALMOST NOTHING. A LIBRARY THAT LINKS TELLS
  YOU EVERYTHING.** `libpcsx2.a` built on the first real attempt and that result
  was nearly worthless on its own: a static archive resolves no symbols, so it
  cannot report a single missing host function. Cabinet's own comment on
  `CabinetPS2Smoke.cpp` says exactly this and it is why that file exists.
- **THE CHEAPEST LINK TEST WAS ALREADY IN THE TREE.** `pcsx2-gsrunner` is
  upstream's own Qt-free frontend, one file, 1332 lines, implementing the whole
  `Host` contract. Building it proved linkability in 2.2 seconds and needed no
  code from us. **Look for upstream's second frontend before writing a smoke
  test** — PPSSPP, Dolphin and RPCS3 all have one too.
- **A SHARED OBJECT LINKS HAPPILY WITH UNDEFINED SYMBOLS**, then fails at
  `dlopen` naming only the FIRST one. That is the worst possible instrument for
  sizing a job: it says "you are missing `g_host_hotkeys`" whether you are
  missing one symbol or two hundred. **`-Wl,-z,defs` makes the linker refuse and
  name them all**, which turned "some unknown amount of host layer" into 57.
- **AND THE FIRST ONE IT NAMES IS A VARIABLE, NOT A FUNCTION.**
  `g_host_hotkeys` is a global the frontend must define. Anybody grepping the
  `Host::` namespace for it will not find it.
- **`find_package(X11)` SUCCEEDS WITHOUT `libXi-devel`** and then the build
  fails at CMake GENERATE time, after "Configuring done", on a missing
  `X11::Xi` target. It reads like a CMake bug. It is a missing package.
- **FEDORA SUPPLIES WHAT CATALYST COULD NOT.** Cabinet hand-cross-compiled ten
  dependencies with pinned tarballs and SHA sums; Fedora 44 met every version
  constraint PCSX2 states, with one exception (`libbacktrace`, which is an
  option). **Check the distribution before believing a port is hard** — the
  difficulty recorded in a reference implementation is usually the reference
  platform's, not the problem's.
- **PCSX2 REFUSES TO START WITHOUT ITS `bin/resources` FOLDER** — game database,
  fonts, GS shaders. It does not degrade, it says "Resources directory is
  missing" and stops. The same shape as PPSSPP's 13 MB of system files.
- **UPSTREAM'S SECOND FRONTEND IS A LINK TEST, NOT A SHORTCUT TO A RUNNING
  GAME.** `pcsx2-gsrunner` looks like a headless PCSX2 and is not one: it
  replays GS dumps and refuses anything else at
  `VMManager::IsGSDumpFileName`. It proved the library links and it is the best
  `Host` reference there is; it will not boot a disc. **Check what upstream's
  harness is FOR before planning a measurement around it.**
- **A TOOL THAT PRINTS FORTY LINES AND THEN EXITS 1 HAS NOT NECESSARILY GOT
  FAR.** gsrunner's `LoadStartupSettings()` resets the console log level from
  empty settings at the end of config init, so every `Console.Error` after that
  point reaches nobody — including the one naming the actual problem. The
  directory listing that precedes it is the last thing you see and it looks
  like progress. **When a program goes quiet at exactly the same place every
  time, suspect the logger before the logic.**
- **A SEPARATE BUILDER CONTAINER WAS THE RIGHT CALL.** PCSX2 needs about thirty
  packages the frontend does not. Putting them in `frontend/Containerfile` would
  have slowed every one of the twenty-one core builds to serve one thing.

### About the product

- **A truncated explanation is worse than none.** A tile's second line holds
  about sixteen characters beside a cover. Measure the column before writing the
  string.
- **Two tiles that read the same are one tile.**
- **`RETRO_DEVICE_INDEX_ANALOG_BUTTON` IS NOT A STICK.** It is libretro's third
  analogue index and its `id` is a joypad button id — L2 is 12, R2 is 13. Any
  code that tests for the LEFT index and treats "everything else" as the right
  stick answers "how far is the trigger pressed" with the right stick's Y axis.
  That is what this console did until 2026-09-19.
- **AND ANSWERING IT WRONGLY IS WORSE THAN NOT ANSWERING IT.** Flycast reads
  the analogue trigger first and falls back to the digital L2/R2 bit **only
  when that value is exactly zero**. Cabinet returns a plain 0 here and
  therefore works by taking the fallback; a real pad's right stick rests a few
  hundred counts off centre, which is not zero, so the fallback never ran. On
  Dreamcast those triggers are the accelerator and the brake.
- **A BUTTON THAT DOES NOTHING IS USUALLY CORRECT.** A RetroPad has sixteen
  inputs and a real machine has fewer. The Dreamcast pad has no shoulder
  BUTTONS at all — its L and R are analogue triggers, so on a Switch Pro
  Controller the top shoulders are meant to be silent and ZL/ZR are the
  triggers. The console prints this per game now:
  `[input] port 0 does nothing in this game: ...`, which is the half that
  answers the question somebody actually asks.
- **`catalog::coverageFor` answers FOUR different questions.** No core exists, a
  core exists and Cabinet does not ship it, this console has not built it, and
  it is built and cannot be driven. Collapsing any two hides work.
- **The core-file naming rule now lives in THREE places** —
  `catalog::coreFileName`, `cores/build-core.sh` and
  `ci/stage-image-payload.sh` — and the comment is on all three. A manifest
  name already ending in `_libretro` does not get a second one. Getting it
  wrong cost every arcade game on this console for a day.
- **An unanswered libretro core option is NOT the default.** The core skips the
  case and the C global keeps its zero value. **This is a demonstration, not an
  argument**: run `--core-options-off` and launch a PSP game and it ends at
  *"the core needs a render target this context cannot build"*.
- **A core that declares no options is the suspicious case, not the clean one.**
  FBNeo and MAME declare theirs per driver, so the table does not exist until a
  game is loaded.
- **`av_info` is a narrow probe.** Geometry, frame rate, sample rate.
- **Ask the CORE, never the platform**, whether an archive should be opened.
- **Never dispatch on a file extension.** Thirty-two files in the reference
  library have none. Sniff the magic bytes.
- **A core reports TWO geometries and neither is wrong.** MAME 2003-Plus
  declares 224x256 in `av_info` for Arkanoid — the picture as SHOWN, already
  turned — and hands back 256x224 from `video_refresh` every frame. A layout
  must use the second. The `[core] WxH` line at load prints the first.
- **A TURNED BOARD'S DECLARED ASPECT IS ALREADY TURNED.** FBNeo says 0.75 for
  DoDonPachi while handing back 448x224. Inverting it turns the picture twice
  and stretches it, which Cabinet shipped once and wrote down.
- **A core calling `SET_ROTATION(0)` is ordinary, not a no-op to ignore.**
  Flycast does it explicitly, which silently overwrote a rotation forced in for
  a test. Anything per-game must be cleared in `loadGame`, not in `load`.
- **Only ARCADE cores ever rotate.** MMagTech, 2026-09-19: a console was built
  to put its picture on a television the right way up. It is what bounds the
  rule above to boards, where it is safe.
- **Rotation does not come from a MAME DAT and never did.** Checked in
  Cabinet's own tree because it was raised as a likely memory: the three
  MAME-derived JSON files it ships hold `rotary`, `dial`, `trackball`,
  `pedals`, `lightgun` and `paddle` — control panels, not screens.

### About the machine and the work

- **A dark first capture is usually a slow boot.** PlayStation needs about
  6000 frames to clear the Sony logo on this VM, Saturn 2600, Sega 32X 2000,
  Neo Geo Pocket 1200. **And a Saturn capture is not repeatable** — the same
  2000-frame run gave max=209 and then max=8.
- **A core will happily "load" something that is not a game.** Genesis Plus GX
  accepted an HTML error page, reported correct Master System geometry, ran,
  and drew black for three thousand frames.
- **A silent fallback is worse than a failure.** Flycast substitutes its own
  boot ROM when it cannot find the real one and says nothing at any log level.
- **Build a core, then RUN it.** Five for five — and the fifth was the worst,
  because nothing was newly built at all: every one of the 223 arcade games
  turned out to be unable to start, and had been for a day. **A green build and
  a passing screenshot say nothing about whether a game runs.**
- **A fact carried across is a fact nobody has checked.** Three of the eight
  rows in a save-path table taken from a working implementation were wrong on
  this console. All three failed silently. Two minutes of `find` after a launch
  caught all three.
- **Telling a core about SOME of its ports is the same as telling it about
  none.** Flycast returns early from `retro_set_controller_port_device` while
  any of its four ports is unset.
- **Measure rather than reason, where you can.** Two minutes of measurement has
  beaten a plausible argument every time it has been tried here.
- **Run the control.** `--core-options-off` and `cores/backend-diff.sh` exist
  for it, and the control has now been more informative than the result three
  times — most recently the bare-`fedora:44` library sweep.
- **Every scripted edit must assert its anchor.** A patch that matches nothing
  leaves a green build with the fix absent.
- **A field added to the middle of a positional struct re-assigns the rest of
  the row.** `catalog.cpp`'s table is positional.
- **Two podman containers with `:Z` over overlapping paths will break each
  other.** `:Z` relabels the whole mounted tree for one container's SELinux
  category. Cost one PPSSPP build. **Do not start a second container over a
  parent of a running one.**
- **`/tmp` on the VM is a small tmpfs.** Copying 273 MB of cores into it fails
  with "Disk quota exceeded" halfway. Use `/var/mnt/games/` for anything large.
- **`pgrep -f "some string"` matches your own command line**, and so does
  `pkill -f`. **Match on something the checker cannot contain** — `pgrep -x`, a
  pid file, or the exit status of the thing you started. `pgrep -x` also
  refuses names over 15 characters, so `cabinetos-frontend` needs
  `ps -eo args | grep "[c]abinetos-frontend"`.
- **`ci/base-watch.txt` now watches the CORES' libraries too**, added
  2026-09-19 off a real `ldd` sweep rather than guessed at — including
  `libX11` and `libXext`, which are PPSSPP's and which nothing had written
  down. Three of the frontend's own were missing from that list as well. The
  image build would now fail rather than ship broken, but it would fail with
  no obvious cause; this is what makes the base-bump pull request say "read
  this" first.
- **A `pull_request` EVENT CAN SIMPLY NOT FIRE, AND NOTHING SAYS SO.** Seen
  2026-09-20 on a pull request that changed `frontend/src/catalog.cpp` as well as
  docs, so `paths-ignore` did not apply: zero workflow runs were created, the
  branch showed *"no checks reported"*, and the pull request sat at
  **`MERGEABLE / CLEAN` with nothing having built it.** A green-looking pull
  request that ran no checks at all is the most dangerous state this repository
  can be in. **`gh pr checks` printing nothing is not the same as passing** —
  count them. The documented remedy works: close the pull request and reopen it.
- **A DOCUMENTATION-ONLY PULL REQUEST CORRECTLY RUNS NOTHING**, because
  `build.yml` ignores `**.md` and `docs/**`. That is expected and is a different
  thing from the fault above — check WHAT the pull request touches before
  deciding which one you are looking at.
- **AND THERE IS A THIRD CAUSE, WHICH IS JUST A RACE.** `gh pr checks` says
  *"no checks reported"* for the first few seconds after a push, between the
  workflow run being created and its jobs registering against the new commit.
  It is indistinguishable from the real fault by that command alone. **Tell them
  apart with `gh run list --branch <branch>`**: a run in `in_progress` means
  wait, and no run at all for the new head means the event did not fire — which
  is the one that needs the pull request closed and reopened. Seen 2026-09-21,
  where it briefly looked like the dangerous case and was not.
- **The weekly base bump needs two clicks, not none.** It opens a pull request,
  but the build on it lands as `action_required` and waits for approval —
  `gh api -X POST /repos/MMagTech/cabinetos/actions/runs/<id>/approve`. And
  **read the relevant list**: when something breaks on real hardware, look at
  what `ci/base-watch.txt` does not watch.
- **A core build failing is not always the core.** `Curl error (28)` is the
  network. Re-run before reading anything into a single red core. This matters
  more now: twenty-one core builds gate every image build.
- **GitHub serves some repositories at 55 KB/s over git and 9.8 MB/s over
  HTTPS.** `build-core.sh` clones `--filter=blob:none` — but **NOT for
  submodules**, where the lazy blob fetch is thirty times slower than cloning
  them whole. The comment in the script says so; do not "tidy" it.
- **RPCS3 will not install a PKG or firmware unless you say `--headless`**, and
  it **exits 134 after a successful install** — SIGABRT in a static destructor,
  *after* logging `Successfully installed`. **Read the log line, not the exit
  code.**
- **An interrupted PS3 install leaves the partial tree behind**, and nothing
  cleans it up. Delete the title directory before retrying.
- **`timeout` does not kill RPCS3 under flatpak.** It signals the `flatpak run`
  wrapper. Follow it with `pkill -x rpcs3` — and mind that a `pkill` aimed at a
  stuck process will also kill an install you started in the same breath.
- **Flathub stalls from this network, silently.** A retry loop fixes it, because
  ostree resumes:
  `for i in $(seq 1 30); do timeout 240 flatpak install -y --user ... && break; done`.
- **The image build still only runs on a pull request aimed at `main`.** If you
  target something else, it needs `gh workflow run build.yml --ref <branch>`.
- **Retargeting a pull request does not re-run CI.** Close and reopen it.
- **`core-manifest.json` IS on GitHub**, at `docs/core-manifest.json` in
  Cabinet, and has been since `37ca75d`.

### AND THE LOGS ARE READABLE AFTER ALL — worth knowing, it cost time today

`journalctl -u cabinetos-session` shows ONLY systemd's own start/stop lines and
none of the frontend's output, which reads exactly like a console that does not
log. It does. The frontend's lines are in the journal without that unit
attached, so ask for the journal itself and grep it:

```
sudo journalctl --since "-40 min" -o cat | grep -aE "\[launch\]|\[core\]|\[frontend\]"
```

That one command is the difference between diagnosing a launch failure in a
minute and guessing at it for twenty.

## The answered questions people keep reopening

### PS3 STORAGE is answered. PS3 still cannot be PLAYED

**Read that twice**, because a heading saying "DONE" cost a conversation on
2026-09-19. What was measured is the STORAGE question and nothing else. Two
games were installed and booted far enough to prove the PKG could then be
deleted; **neither reached gameplay and neither could, because the test VM has
no GPU.** Playing a PS3 game needs a Vulkan path in the host, which is open
question 20 — **and the A9 Max is the machine that makes it possible.**

**GameCube and PS2 do not run on this console either** — 85 games between them,
both reported as *"the core for this system is not built on this console yet"*,
because Dolphin and PCSX2 are not libretro cores and neither has been built
here. Cabinet plays both on the Mac by embedding them, which is exactly what
makes it easy to believe they work here. They do not.

**Installing a PS3 game does not cost a second copy of it**: the installed game
is the same size as the PKG, so once the PKG is deleted the game costs what any
other game costs. Measured both times at a ratio of 1.00x, once on a 19.8 GB
title. PROJECT.md, open question 19.

**A decrypted ISO is the better shape where it exists**, and it needs nothing
from this console — provided it carries a 20-byte PS3 disc header that
`xorriso`, `mkisofs` and `hdiutil` do not write. Two tools were handed to
MMagTech on 2026-09-18 and are NOT in this repository. **It only reaches six of
the thirty titles**; 24 are PSN PKGs with no disc behind them, so the install
route is the majority case and not a fallback.

**DECIDED 2026-09-18: this console reads a PKG or a stamped ISO, and NOT a disc
folder.** A disc folder is hundreds or thousands of files — Mass Effect 2 is
**8,337** — and downloading a tree that size from RomM is a transfer path that
does not exist here.

### The on-disk folder layout is done, and so is the second drive

`frontend/src/storage.{h,cpp}` owns it. The root is `/var/lib/cabinetos` on a
console and the working directory on the VM. **One spelling of a platform,
everywhere: RomM's `fs_slug`** — it is also the only one that is unique, since
two Arcade platforms share the slug `arcade` with 223 games between them.

**An entry whose core opens its own archive stays a directory holding the
server's own file name**, because MAME and FBNeo pick their machine from the
loaded file's NAME and the layout was renaming it.

**Plug a second drive in and it is used.** Demoting a kept game is a RENAME and
not a copy — proved by device and inode, `58:82064` both times.

**The one thing still missing is the screen** that says a drive is not
connected. The console says it on stderr, once.

## Things the user wants discussed, each in its own session

### THE UI PASS HAPPENED — 2026-09-21. This is what it left.

It was a whole session, driven from the television, and the loop it was built for
(`tools/ui-loop.sh`) is what made it possible: about forty builds, each one
looked at on the panel before the next.

**THE CONSTRAINT HELD.** MMagTech, early on: *"these are just lessons i want this
to still remain cabinet distinct."* Nothing here is Valve's look. The purple is
still the ground, focus is still a rim, and the one place a Steam idea was taken
whole — the focused thing lighting the screen — was deliberately made to sit
UNDER the Cabinet gradient rather than replace it.

#### What changed, and where the reasoning is

Every one of these is argued for at its definition in `frontend/src/design.h` or
at its draw site. This is a list, not the record.

- **HOME LOST ITS HERO.** It was an 1800 × 340 card holding a 3:4 cover with the
  same cover blurred either side to fill what it could not. The most recent game
  is the first card on Recent again, focus opens there, and A launches it
  straight — resume-first is one rule now instead of a separate object with its
  own button. Covers grew from 158 × 210 to 240 × 320 with the room it freed.
- **THE FOCUSED GAME LIGHTS THE ROOM.** Its cover, blurred to a colour field, over
  the purple gradient and under a scrim, on Home, the Library, the grid and
  Search. It waits 220 ms before it moves so a controller running along a shelf
  does not strobe it, and cross-fades over 600 ms of ease-in-out.
- **THE TOP BAR IS CHROME ON EVERY BROWSING SCREEN**, with its cursor owned by
  the app. L1/R1 walk the destinations. See PROJECT.md's navigation section.
- **SEARCH EXISTS.** Live substring filter over the library already in memory,
  with the keyboard docked at the bottom of the screen — `Keyboard::Config::
  dockedBottom`, which is also where four rejected panel treatments are recorded.
- **THE GRID FITS TWO WHOLE ROWS**, which it could not before: captions were 80
  points of every row and 772 of usable height over a 481-point row is 1.6.
- **A KEPT GAME IS MARKED** on its cover, everywhere a cover is drawn.
- **NAVIGATION SOUNDS**, synthesised rather than sampled — `frontend/src/sound.h`
  has the three reasons. The off switch is built; Settings will own it.
- **HOLDING A DIRECTION REPEATS AND ACCELERATES**, which it never did — the pad
  sends one event per press and nothing was driving a repeat.
- **L2/R2 JUMP BY LETTER** in a grid, with an index down the right.
- **THE ART WAS A THUMBNAIL EVERYWHERE.** RomM keeps covers at 162 × 216 and
  810 × 1080 and this console asked for the small one — including for the launch
  screen's 340 × 460 cover, a 4.2× upscale. Fixed where it is drawn large.
- **YOUR AVATAR IS ON SCREEN.** It never was; see `romm::User::avatarPath` for
  why the obvious path is a 404 and what actually serves it.
- **A DOWNLOAD NO LONGER COVERS THE SCREEN.** Progress is on the row that started
  it, plus a corner readout in the bar. It auto-launches only if you are still on
  the screen you pressed Play from.
- **A LAUNCH REFUSAL IS VISIBLE.** It used to go to stderr only — which is how
  item 9d went unexplained.
- **EVERY SYSTEM FILLS THE HEIGHT** at its true aspect. PROJECT.md, "The canvas".
- **THE RENDERER GAINED TWO THINGS**: `setContentAlpha` for screen transitions and
  `setScissor` for scroll windows.

- **A CURTAIN ON LAUNCH AND EXIT.** `Core::loadGame` blocks the frame thread, so
  the old transition was a UI frame, a UI frame, several hundred milliseconds of
  nothing, then a game frame — no animation can live in a gap where no frames
  are drawn. The curtain closes over 260 ms FIRST, the blocking work happens
  behind it, and it lifts over 420 ms. It does not close for a background
  download, and it lifts again on a refusal.
- **THE PAUSE MENU ANSWERS BACK.** Save state and Load latest were wired and had
  been for days; every outcome of both went to stderr and nowhere else. See
  MenuNotice, and docs/CABINET.md for the reference's own wording, which is
  better than anything invented here.
- **AND LOAD LATEST WAS ASKING THE WRONG MACHINE.** It only ever queried RomM, so
  saving and immediately loading raced the background upload and found nothing,
  in silence. The server is still the source of truth — that is Cabinet's rule
  and it is right, because states live on RomM so "latest" can mean latest
  across devices — but Save now reports when the upload actually lands, and a
  server with nothing for this core falls back to this machine's newest state.

#### What is left, and it is short

- **WHETHER THE CARTRIDGE-ERA CORES KEEP "SAVE STATE" IN THE PAUSE MENU IS OPEN,
  AND IT IS A UI DECISION.** Open question 25 removed save states from PS2,
  GameCube and everything after on 2026-09-21, and **explicitly left the
  twenty-one older cores open** — there a state is often the only way to stop
  mid-level, and it is the idiom every emulator frontend uses. So the menu would
  offer different items on different systems, which needs deciding rather than
  assuming. **The defensible principle is *states exist where the system has no
  save of its own*** — roughly the cartridge/disc line, but per-GAME in truth,
  and a rule that is nearly right is how a console ends up feeling arbitrary.

  **It belongs in this session, not in an emulator change.** Open question 25
  says so directly: it touches 21 working cores, the pause menu's shape and what
  Cabinet's other platforms expect in a RomM row, and it is one conversation
  with open question 23 and the UI pass rather than three features.

- **SAVE AND LOAD STATE ARE UNVERIFIED BY A PERSON.** Everything above was built
  and compiled at the end of a long session and MMagTech had not yet retried it.
  **Do that first**: save in a game, load it back, and watch what the menu says
  at each step. The words are the feature as much as the bytes are.
- **THE PAUSE MENU GETS SHADER AND GLOW, BECAUSE CABINET ALREADY DID.**
  MMagTech asked whether to keep the pause menu quick and add a deeper
  RetroArch-style one for shaders, the performance mode and glow strength. The
  first answer given here argued from first principles that those belong to the
  console and therefore to Settings. **That was wrong, and the reference had
  already settled it** — MMagTech: *"in cabinet os we have shader and glow as
  part of the menu and its not even in here."*

  `TVPlayerView.pauseMenu` is **Quit, Shader, Save state, Load latest state,
  Resume**, in that order. Shader is a submenu of candidates; glow is an
  Off/Subtle/Strong picker. Both sit over the paused game, which is the whole
  point: you are adjusting how the picture looks and the picture is right there.

  **AND A SHADER IS PER PLATFORM, NOT GLOBAL.** `NativeShader.current(for:)` and
  `setCurrent(_:for:)` are keyed on the platform. A CRT filter on Super Nintendo
  and none on PlayStation is a real choice and the console is the wrong scope
  for it. "Global versus per-game" was the wrong axis — the answer is PER
  SYSTEM.

- **THE GLOW ALREADY EXISTS HERE AND CANNOT BE REACHED.** `--glow off|normal|
  strong`, peaks of 0.0 / 0.025 / 0.04, drawn by `Renderer::drawBiasGlow` with
  its own shader program. A command-line flag and nothing else. Cabinet's three
  levels were live-tuned on device with a slider on 2026-08-13 and then frozen
  into a picker, so **reconcile the numbers against Cabinet's rather than
  inventing a second set**.

- **WHAT IS STILL A SETTINGS SCREEN**, and it is short: the one quality setting
  (open question 23, which really is global), and the interface sounds' off
  switch — built and waiting for an owner, `sound::setEnabled`. That is not
  enough to justify a second in-game menu, which was the one part of the first
  answer worth keeping: RetroArch's problem is not depth, it is two menus with a
  boundary nobody can remember.

- **THE OLD SETTINGS NOTE.** The last bar item that does nothing, and open question 23's one
  quality control has nowhere to live until it exists. **This is the next UI
  session.**
- **THE LAUNCH SCREEN IS TWO THIRDS EMPTY.** Reviewed and not acted on: the cover
  is centred with about 300 points of nothing above it, the rows stop 340 short
  of the right edge, and the metadata is two facts. The server holds `summary`,
  IGDB genres, release date and player count, and **six screenshots per game**
  — measured, 40 of 40 — and this console asks for none of it.
- **THE BAR SAYS WHICH DESTINATION YOU ARE IN TOO WEAKLY.** Selected and focused
  are two tints of one capsule, 0.35 against 0.25, which is a difference you can
  measure and barely see from a sofa. A cyan underline was tried and withdrawn
  the same minute — *"nevermind drop that looks bad"*. Whatever answers it, it
  is not a second colour on the bar.
- **THE PLATFORM TILES WERE NEVER REVIEWED.** The session turned to the game grid
  instead. They are still a box with a small picture in it, which is the idiom
  Home just stopped using.

#### Two things about how this session worked, worth keeping

**THE LOOP PAID FOR ITSELF IN THE FIRST HOUR** and then twice more: a stale
deploy after a failed compile cost a cycle before `ui-loop.sh` was taught to
delete the artifact first, and a capture that showed the previous build sent a
complaint chasing a fault that was already fixed.

**JUDGE ON THE PANEL, AND THE PANEL DISAGREED WITH THE CAPTURE REPEATEDLY.** The
bar's spacing, the backdrop's strength, the keyboard's background and the
guillotine at the top of a scrolling screen were all invisible in a PNG and
obvious on a television. The rule at the top of this file is not a formality.


### One quality setting for the whole console — open question 23, NEW

Raised 2026-09-20: *"I hate messing with settings in emulators. What I'd want
me or anyone to experience is something like a performance and quality setting
that affects all cores."* **Surveyed the same day and the scope is much smaller
than it sounds: seven systems, six cores.** The other seventeen either have
only a look filter or nothing to choose at all.

Two things the survey corrected, both of which would have been guessed wrong:

- **It is NOT "the hardware-rendered cores".** PlayStation renders in software
  through `pcsx_rearmed` and still has four real levers. Dolphin declares no
  options at all until a game is loaded, so it is invisible to a survey taken
  at load time.
- **The 2D cores' `overclock` options are ACCURACY, not quality**, and a SNES
  is not a performance problem on any machine this runs on. Dragging them in
  would make the setting mean two different things.

**THE AUDIT IS DONE and it is `docs/CORE-OPTIONS-AUDIT.md`** — all 23 cores,
825 options, read out of the loaded `.so`. **Eight systems have a resolution
worth raising and for each it is ONE option**: PS2, GameCube, PSP, Dreamcast,
N64, PlayStation, 3DO and FBNeo arcade. Everything else sorts into free-and-
better, correctness-per-game, accuracy-set-once, or look — none of which
belongs behind a performance tier.

**The obstacle is not the code, it is that there is only one machine to tune
against and it has 6 to 10x more headroom than it needs.** A Performance level
tuned on hardware that never needs it is a guess wearing a number. Get the
DEFAULT right per platform first — the console can already read its own speed
off the core's audio and say "this ran at 72%" on a machine that cannot keep
up, which is more useful than a settings page.

**And when you measure the cost, do it with a WARM SHADER CACHE.** A single run
lies: 2x native measured slower than 4x on the same machine in the same
session, purely because the 2x run compiled shaders. The audit keeps that wrong
table on purpose.



### Sleep, screen blanking, and what a console does when nobody is playing

Raised 2026-09-20: *"we currently have no screen or sleep behaviour, the console
just stays active all the time."* **Open question 10b** has the measurements —
`IdleAction=ignore`, `IdleHint=no` permanently, the panel still lit after nearly
eleven hours, and every connector exposing a `dpms` node that nothing writes to.

**There is no idle handling at all**, and nothing measuring idleness for
anything to act on. Two things already in PROJECT.md point at the gap: open
question 10 assumes *"a machine that stays awake and blanks its display"* is the
likely default, and the image deliberately keeps `ds-inhibit`, whose entire job
is making idle detection behave properly on a machine that does not detect idle.

**Take it with the CEC work**, because the console turns the television on and is
woken by it, and blanking our output while the set stays on is a different
behaviour from letting the set sleep while we stay lit. The two answers have to
agree.

### Switch, or Xbox

Asked for 2026-09-19: *"for our next session I'd like to discuss implementing
switch or xbox."* Do not start building either as a side effect of something
else, and read these numbers before the conversation opens:

| | Games in the reference library | Size |
|---|---|---|
| **Switch** | **109** | 310 GB, largest title 28.3 GB |
| **Xbox** | **0** | not in the library at all |
| Xbox 360 | 0 | not in the library at all |

**Switch serves 109 games today and Xbox serves none.** Both land on the same
unanswered question rather than a new one: **neither is a libretro core.** Every
one of the twenty-one cores here is a `.so` this frontend loads and drives in
its own frame loop; Switch and Xbox emulation lives in standalone applications
with their own windows, input and renderers — the same shape as PS2 and
GameCube, which is **open question 12**, and the same shape as open question 21
on emulators that cannot be baked into the image.

The recommendation on record, unchanged: **PS2 and GameCube first**, because
they are already in the plan and already have a proven answer in Cabinet, then
judge the heavy systems with that experience in hand. Switch also brings the
storage question: **a single 37 GB title is larger than the free space this
console keeps in reserve**, and the cache, both floors and Download All were
all designed against cartridge and disc-sized games.

### Account switching

RomM has users; tvOS already switches between them. Raised 2026-09-16 with the
words "we would implement it slightly different", and explicitly deferred to a
session of its own. Read Cabinet's tvOS account handling and
`Auth/Keychain.swift` first (the token is already keyed by server host), then
**ask what the difference is** before writing anything. It touches things
already built: Home is assembled from RomM's play history, and favourites and
recents are RomM's rather than local.

**And it now has a second half.** The token lives at
`~/.config/cabinetos/romm.json` under the session user, and the server address
lives in `/etc/cabinetos/session.env`, which is one machine-wide file. Neither
shape has anywhere to put a second account.

## Licensing, which is now written down

**`docs/LICENCES.md`** lists every core, its licence, its upstream and the
commit this project pins.

**The one line that shapes decisions:** six of the twenty-one cores — FBNeo,
MAME 2003-Plus, Snes9x, Genesis Plus GX, PicoDrive and Opera — are free for
**non-commercial use only**. CabinetOS is free, is not sold, and takes no
donations, and that is what keeps them legitimate. **Selling a machine with this
image on it would break it**, which matters because the hardware has already
changed once and may change again.

**The terms ship INSIDE the image**, at `/usr/share/licenses/cabinetos/`,
installed and asserted by `build_files/build.sh` — because whoever pulls the
image is exactly the person who never sees this repository. **That argument got
stronger on 2026-09-19**: the image now also contains the twenty-one binaries
those terms are about.

Still owed: the licence text readable on the console (Settings → About), and a
verification pass over each line against the source it came from — they were
carried across from Cabinet's list rather than checked here, and this project's
own rule is that a fact carried across is a fact nobody has checked.

## Cabinet-side debts

1. **Flycast carries unscripted edits in its working tree**, so its pinned
   commit does not reproduce what ships, and **that is the only reason Flycast
   cannot share its emulator tag.** Capture the diff before anything touches
   that tree:
   `git -C spikes/cores/flycast/src diff > tools/patches/flycast-unscripted.patch`
2. **The manifest does not describe how a core is built.** PPSSPP's entry says
   `patches: null` and `build_args: null`; `tools/build-ppsspp.sh` applies two
   source patches and passes CMake flags, two of which change what the binary
   is. **The builder scripts are the real record**, and the manifest is
   load-bearing for parity — so this is worth a pass across every core.
3. **A comment in `NativeCore.savesOverSaveRAM` says PSP save sync is "its own
   future feature".** It was built afterwards and the comment never moved. It
   cost a wrong claim in a pull request here. **A stale comment reads exactly
   like a current one.**
4. **mGBA's Mac build is `-dirty` too**, and its manifest entry lists no patches
   at all. Same problem, quieter.
5. **Two "unrecoverable" tvOS revisions were recovered with `strings`.** Nine
   more are probably sitting in the shipping archives.
6. **melonDS's archives carry no revision** while the same upstream built here
   reports one, so something in Cabinet's build is losing `GIT_VERSION`.
7. **NEITHER PS2 NOR GAMECUBE HAS A FRESHNESS RULE, AND IT HAS PUT SIX EMPTY
   CARDS ON THE SERVER.** Measured 2026-09-20: of seven rows, only Burnout 3
   held a save. `PS2MemoryCard.store` and `GCMemoryCard.store` both force the
   first upload for a game regardless of content —

   ```swift
   let neverUploaded = stamp(romId: rom.id) == nil
   guard neverUploaded || digest(bytes) != digestBefore else { return }
   ```

   — and nothing anywhere asks whether the card holds anything. The
   `neverUploaded` clause exists for a good reason (a card adopted from PCSX2's
   shared `Mcd001` arrives already containing a save and never looks "changed")
   but it opens this hole. **Same fault the Dreamcast path has**, which
   `catalog.h` already records; worse ratio. CabinetOS's two rules are in
   `filesave.cpp` and are cheap to port: a PS2 card without the
   `Sony PS2 Memory Card Format` magic is untouched, and a GameCube card with
   no directory entry in blocks 1 or 2 is untouched.

8. **`MAIN_MEMORY_CARD_SIZE` IS NOT PINNED, AND THE CARD'S SIZE IS IN ITS
   FILENAME.** `CabinetDolphinHost.cpp` sets `MAIN_SLOT_A` and
   `MAIN_MEMCARD_A_PATH` and leaves the size at -1, so Dolphin decides — and
   one of the three cards on the reference server is `cabinet-934.USA.251.raw`,
   a 2 MB card, beside two 16 MB ones. RomM matches a row by filename, so the
   day Dolphin changes its mind about a game's card size the save lands under a
   new name, gets a new row, and the old one is orphaned. **Not observed**, and
   the same shape as the `.USA.` suffix the existing comment describes finding
   by accident. CabinetOS pins it.

9. **`PS2PlayerView.swift`'s header says the screen has no pause menu and no
   save state. It has both.** The file opens with *"there is no sound, no
   controller, no pause menu, and no save state or memory card sync — this
   screen exists to put a picture on the display"*, and forty lines later there
   is a four-row menu whose Save and Load call `CabinetPS2SaveStateToSlot(1)`
   and `CabinetPS2LoadStateFromSlot(1)`.

   **Same shape as 4 and as the `savesOverSaveRAM` comment in the handover's
   list**, and it cost the same thing again on 2026-09-20: a stale comment reads
   exactly like a current one, and the comment is what got remembered rather
   than the code. The memory-card half of that sentence IS still true, which is
   what makes the rest of it convincing.

## Solved, and kept for the lesson

**Finished work, with its investigations intact. Nothing here is queued and
nothing here is owed.** It sits at the back so the queue above holds only live
work, and it is kept rather than deleted because several of these records are
worth more than the fault they closed — what ruling something out actually
looked like, and three separate cases of measuring in a configuration nobody
plays in.

**Cross-references elsewhere in this file still point here by number** — "see
item 3b", "item 9c", "item 1b". No item was renumbered when it moved.

### PLAYSTATION 2 AND GAMECUBE PLAY — 2026-09-20

**1232 playable games, up from 1147.** Both systems draw a picture on the
television off the A9's Radeon. The work is in the `vulkan-host` branch.

**It was never about the emulators.** Both libretro cores existed, both already
had Vulkan compiled in, and `catalog.cpp` has routed `ps2 -> pcsx2` and
`ngc -> dolphin` since the table was written — so two `.so` files in a core
directory turned "not built on this console yet" into 85 playable games with
no code change at all. What was missing was **this frontend's half of a
contract libretro already specifies**: the instance, the device, the queue and
somewhere to put the picture. RetroArch implements that end; this console owns
its frontend and had only ever done the OpenGL ES half.

See open question 20 for the whole thing. The three one-line faults:

1. `GET_PREFERRED_HW_RENDER` was hard-wired to GLES, so **Dolphin never asked
   for the Vulkan it has compiled in**.
2. `SET_HW_RENDER` refused Vulkan by name.
3. **Dolphin checks for a `VkSurfaceKHR` to decide whether it has a display.**
   With none it renders and never presents — fifty seconds of emulated Mario
   Kart, correct audio, a black screen. It gets a `VK_EXT_headless_surface`.

**MEMORY CARDS: PS2 TRAVELS, GAMECUBE DOES NOT YET.** A card written on the Mac
lands on the console and the game reads it — proved with Burnout 3, which is
the only real save that exists for either system. GameCube saves and syncs but
uses this console's own row naming; see open question 12b for why that is
deliberate.

**SIX OF THE SEVEN CARDS ON THE SERVER WERE EMPTY** and were deleted at
MMagTech's request on 2026-09-20. Cabinet for Mac uploads a card on first play
regardless of content and has no freshness rule for either platform — the same
fault `catalog.h` records for Dreamcast, worse ratio. **CabinetOS now refuses**;
both rules are measured and in `filesave.cpp`.

**WHAT IS NOT DONE, and none of it is hidden:**

- **Nobody has saved inside a game and watched it go up.** The download half is
  proved; the upload half is the same code Crazy Taxi 2 proved for Dreamcast.
- **THE PS2 *LIBRETRO CORE* STILL CANNOT SHIP AND NEVER WILL** — its source
  repository does not exist. **But PlayStation 2 is no longer blocked**, because
  the embed route was measured on 2026-09-21 and is open: see item 1a. The
  GameCube core can be pinned and that part is unchanged.
- **TWO OPEN BUGS FROM ONE HOUR OF PLAY, both reported by MMagTech and neither
  reproducible from here.** See item 1b.

### 1b. BOTH BUGS FOUND BY PLAYING ARE CLOSED — one fixed, one not reproduced

#### THE TUNNEL IS FIXED — MMagTech, 2026-09-21: *"tunnel was gone on new core"*

It went away with the move to upstream PCSX2, so it belonged to the libretro
core that has since been deleted from the machine — which is also why it never
reproduced from here: **every measurement was taken against the emulator that
did not have the fault.** No change was made to chase it and none is needed.

The investigation is kept below because its conclusion was right for the wrong
reason, and the warning at the end of it is still good advice.

**"It looks like I was looking through a tunnel when racing"**, Burnout 3 on
the television. **Every capture taken here measures a correct 4:3 picture
exactly filling its quad** — at 1920x1080 and 3840x2160, in a menu and in
gameplay, on both bridge routes, with the widescreen hint on and off. The new
`[picture]` line prints the four numbers that have to agree and they agree:

```
[picture] core 640x448 aspect 1.3333 -> quad 1440x1080 (1.3333) at 240,0
          uv 0.0000,0.0000..0.8333,0.8750
```

**So it is not the layout and not the texture coordinates.** "Tunnel" describes
a FIELD OF VIEW rather than an aspect, which points at the emulator's own
settings rather than the frontend — start with the 78 core options, and ask
which game and whether it happens from a cold boot or only after loading the
Mac's memory card. **Do not trust a bounding-box measurement here**: the first
three this session were confounded by the game's own black borders and by the
pause menu's dimming, and one of them sent an hour the wrong way.

#### NOT REPRODUCED, WHICH IS NOT THE SAME AS FIXED: THE PAUSE MENU'S EXIT

**This heading said STILL OPEN and the entry above it said closed.** Both were
written the same day and the second is right: MMagTech, *"you can consider the
hang done as we havent hit it again."* **No change was made that targeted it**,
so the suspect below was never eliminated — it outlived a change rather than
being killed by one. If it comes back, this is still where to look.

**THE PAUSE MENU'S EXIT LEFT THE CONSOLE STUCK.** The menu was on screen with
"Exit to Home" focused and the process was **asleep at 0% CPU** — and the UI
loop is unpaced, so 0% means the frame loop had STOPPED, not that a button was
ignored. That is a hang in teardown. **It does not reproduce**: exiting at 1500
frames in works headlessly and under gamescope when driven from code. The
suspect is `Core::unloadGame` on a threaded core —
`vk::destroyContext` calls `deviceWaitIdle` on a device the CORE created and
may already have torn down in its own `context_destroy`. **Get a backtrace next
time rather than theorising**: the process is still there, so
`gdb -p <pid> -batch -ex "thread apply all bt 12"`.

### 2. Vertical arcade games play the right way up — DONE 2026-09-19

**`RETRO_ENVIRONMENT_SET_ROTATION` was in `libretro.h` and handled nowhere.**
DoDonPachi and every other TATE board rendered sideways; 223 arcade games were
affected. It is fixed, and PROJECT.md's *Vertical arcade boards, and the turn
they ask for* has the whole thing. The three parts worth carrying:

- **The turn goes on the quad's CORNER in the vertex shader**, before the
  corner looks up a texture coordinate. It cannot go in `u0,v0,u1,v1` — those
  flip, they never transpose.
- **A turned board's declared aspect is ALREADY turned.** FBNeo says 0.75 for
  DoDonPachi while handing back 448x224. Inverting it turns the picture twice;
  Cabinet's own comment says that "stretched every vertical game". So a turned
  picture takes its shape from raw pixels. Safe because **only arcade cores
  ever rotate** — a console was built to output to a television — which is
  MMagTech's point and what bounds the whole rule.
- **A turned picture fills the height at its true shape**, MMagTech's call:
  1080x2160 on the A9, 28% of the width. Upright games keep integer scaling.

**What has NOT been done is look at it on the television.** Every check was a
capture, including one at 3840x2160 off the A9's own Radeon. A person with a
pad is still owed.

### 3. One real in-game save, on Dreamcast — **DONE 2026-09-19**

**A person played Crazy Taxi 2, saved inside it, quit through the overlay, and
the VMU reached RomM.** No save in this class had ever been written by actually
playing a game here; every round trip before this restored a real card, watched
the core read it, and sent back byte-identical bytes, which is the correct
answer for a session that saved nothing and is why forcing an upload needed
`--sync-test`.

```
22:45:24  [save] 131072 bytes from the server into /var/lib/cabinetos/bios/dc/vmu_save_A1.bin
22:48:19  [save] 131072 bytes from dc/vmu_save_A1.bin
22:48:19  [overlay] exited to Home
22:48:20  [save] uploaded flycast-native
```

The middle line is the whole result: it prints only when the card differs from
the baseline taken at launch, so it is the console saying *this game wrote
something*. On the server, `Crazy Taxi 2 (USA) (Cabinet).srm` now reads
`updated 2026-09-20T02:48:19Z` against a `created 2026-08-16` — **the same row
overwritten rather than a second one**, 131072 bytes, tagged `flycast-native`,
which is the row an Apple TV already reads.

**It could not have happened a day earlier**, and that is worth keeping: the
Dreamcast's accelerator and brake are its analogue triggers, and this console
answered "how far is the trigger pressed" with the right stick's Y axis until
the same evening. Crazy Taxi literally could not be driven. See *Things that
will bite you*.

**What is still owed in this area:** every other file-writing platform is
proven by round trip rather than by play — 3DO, Sega CD, Neo Geo Pocket, DS and
both arcade emulators. The mechanism is now known to work end to end, so those
are a matter of playing them.

### 3b. A game that draws nothing — **SOLVED 2026-09-21. It was the SECOND game.**

**THE RULE IS: PLAY A GAME, LEAVE IT, PLAY ANOTHER OF THE SAME PIXEL SIZE.**
That is the whole of it, and it is why two days of deliberate attempts failed —
everybody was reproducing "launch this game" and the trigger was the game
BEFORE it.

`Core::load()` calls `unload()`, which deletes the frame texture and sets
`texture_ = 0` while leaving `frameWidth_`/`frameHeight_` at the previous game's
values. The next game generates a fresh texture with NO STORAGE, `sizeChanged`
comes out false because the dimensions match, and the upload calls
`glTexSubImage2D` on it: `GL_INVALID_OPERATION`, nothing uploaded, a texture
that samples black — while the core runs perfectly and plays perfect audio.

Six FBNeo boards in one session all share a resolution. Air Zonk then Devil's
Crush are both 256x240 and the second was black; Air Zonk then a SNES game is
256x240 then 256x224, so `sizeChanged` was true and it worked. The first game
after a restart always worked, because those fields start at zero.

**Fixed** by zeroing them where the texture is generated — the only place that
knows the texture has no storage, and correct however the texture came to be
missing. Confirmed by MMagTech on TurboGrafx and 3DO.

**Everything the 2026-09-19 investigation established was true and none of it
was wrong** — the core was innocent, the upload "succeeded", the texture was on
the right unit. It was looking at the second game and measuring the first.

**Original report, kept because the shape of it is the lesson.** Found by
MMagTech on the television, 2026-09-19: six FBNeo launches in one session —
DoDonPachi, Deathsmiles, Pink Sweets, ESP Ra.De., Mushihime-sama Futari — drew a
black picture while the core ran normally and played sound. Every launch in
every fresh process since was fine, including deliberate attempts to reproduce
it.

**What was established, and it is a lot:**

- **The core is innocent.** The probe read the buffer it hands over:
  `rgbaMax=248`, a real picture, every frame.
- **The upload is innocent.** `upload ok`, correct texture on the correct unit,
  no GL error.
- **The layout is innocent.** The letterbox glow was drawn from the picture
  rect and its profile put the window at x 1120..2790 on a 3840x2160 panel,
  which is exactly where a 240x320 board belongs.
- **The loop is innocent.** 44% of a core, sleeping in poll, presenting.

So **the frame reaches the texture intact and is lost at sampling**, and the
console keeps drawing everything else perfectly — which is why it reads as
"this game does not work" rather than as a fault.

**Two theories, both killed by measurement**, recorded so nobody spends the day
again: *Flycast poisons the cores after it* (the launch order fitted six for
six, then Crazy Taxi 2 followed by Pink Sweets rendered fine), and *Flycast
leaves a GLES sampler object bound* (it leaves none — probed).

**One loose end**: the first upload after a Flycast session reports
`errBefore=0x502`, a `GL_INVALID_OPERATION` left pending by the teardown.
Unexplained, and not shown to be related.

**THE INSTRUMENT THAT MAKES THIS TRACTABLE, AND IT IS NEW.** The television can
be photographed directly, with the session running and undisturbed:

```
export XDG_RUNTIME_DIR=/run/user/$(id -u) GAMESCOPE_WAYLAND_DISPLAY=gamescope-1
gamescopectl screenshot /var/home/cabinet/now.png
```

**`gamescope-1`, not `gamescope-0`** — the first socket refuses the connection.
That turns "it looks black" into a number: a black game screen measures
**max pixel 8**, which is not black at all, it is the bias glow at 0.025, and
reading that is what proved the geometry was right.

### 4b. First run — BUILT, start to finish

**A person can now set this console up with a keyboard and a phone, and never
touch SSH.** Five screens, the whole chain, on the reference machine.

| | |
|---|---|
| `firstrun.{h,cpp}` | the chain, and every rule about what may be skipped |
| `setup.{h,cpp}` | the five screens, and the workers that keep them drawing |
| `qr.{h,cpp}` | byte mode, versions 1–10, error correction M |
| `net.{h,cpp}` | status, scan, join, forget, and the polkit verdict |
| `bluetooth.{h,cpp}` | the adapter, the scan, and pair/trust/connect |
| `proc.{h,cpp}` | the one place that starts a process, argv only, never a shell |
| `60-cabinetos-network.rules` | the grant that stops Phase 6 breaking Wi-Fi |

**SEE IT WITHOUT DISTURBING THE TELEVISION:**

```
SDL_VIDEODRIVER=offscreen ./cabinetos-frontend --setup-step pair \
  --screenshot /tmp/x.bmp --render-size 3840x2160 --frames 400
```

`--setup` forces the flow on a machine that is already configured and **never
writes anything**, which is the only way anybody here can look at it — both
machines are set up and taking that away to see a screen is a silly way to lose
an afternoon.

**THE CHAIN IS ENFORCED, NOT DESCRIBED.** `Machine` is handed a `Facts` and
judges it; it never calls the network, the disk or a server. `observe()` is the
one place that goes and looks. That is the same split `screens::` makes and it
buys the same thing — the whole flow can be walked at any point in it, on a
machine with nothing attached.

**Which made the rules testable, and the test found a real deadlock on its first
run.** `--first-run-rules` walks all 96 reachable combinations of facts and
asserts the REFUSALS rather than the happy path. What it caught: the Wi-Fi
step's skip was keyed on **Ethernet** being up rather than on being **online**,
and those coincide only while this console knows about exactly two kinds of
link. One `net.cpp` change later, a machine online over a third kind would pass
the network gate and then sit at a Wi-Fi step it could neither satisfy nor skip.
Reading the code again would not have found it.

**THE A9 SAYS IT NEEDS NO SETUP, AND THAT IS THE RULE THAT MATTERS MOST.** A
machine with a server address, a token and a user behind that token is ADOPTED
rather than walked through a wizard, and the marker is back-filled saying so.
Without that rule, the reference console — set up by hand over SSH, working for
a day — would have presented a welcome screen the next time it booted. The
question is *"is this machine configured"*, not *"has this flow been run"*.

**THE QR IS PROVED ALL THE WAY TO THE GLASS.** A capture of the finished
3840x2160 frame off the A9's own Radeon was handed to a decoder with no cropping
and no help — exactly as a phone pointed at the television sees it — and read
back the live pairing URL the server had issued seconds earlier.

### THE WRITES ARE TESTED NOW — `--first-run-writes`

**Completing setup had never written anything, ever.** Every walkthrough used
`--setup`, which forces the flow on a configured machine and deliberately writes
nothing — so `setServerAddress` and `markCompleted` had never once been executed
by the product. **That is the worst failure this feature can have**: a marker
that does not persist means a console completes setup and boots straight back
into setup, for ever, on a machine somebody has just installed. It would look
exactly like a console that cannot be set up at all.

Given that three of 2026-09-20's faults were in code that looked correct and had
simply never run, that was not a risk worth carrying. `--first-run-writes` runs
against a scratch root, touches nothing real, passes nine checks and belongs in
CI. **The token save is the one write not covered, and it is the one that is
already proven** — both machines here were paired with `--romm-pair`, which
calls the same `saveToken`.

**MMagTech will not reinstall until the UI is finished and every core is built
and tested** (2026-09-20), which is the right call — a fresh install is
expensive and should be spent once on something complete. So the fresh install
is the FINAL ACCEPTANCE TEST rather than a prerequisite, and most of what it
would prove can be had sooner:

| | |
|---|---|
| The writes | **done** — `--first-run-writes` |
| An empty server field | **the VM**, with its config moved aside; nothing needs reinstalling |
| An empty Bluetooth list | **the A9, reversibly** — `bluetoothctl remove` the Pro Controller and it has to be DISCOVERED, which is the real first-run case. Re-pairing it through the product is the test. |
| The whole thing on a virgin machine | only a fresh install |

### 6. The UI freeze IS LIFTED

**Decided 2026-09-17: no more UI is designed or tuned until CabinetOS is
installed on the reference machine.** The user's call, and **the condition is
met**: the console runs the frontend on a television, on its own GPU, at the
panel's native 3840x2160. Everything under *Available now that the console runs
on a television* is available, in the order it is written.

**The line is the acceptance test, not the subsystem.** If the test is "does
this look right", it waits. If the test is a measurement or a behaviour, it
goes ahead — and a screen that already exists is not frozen, because fixing
something *wrong* is not the same as tuning something.

### 9c. A BLACK SCREEN ON LAUNCH, WITH AUDIO — SOLVED, see item 3b

**This was the second-game fault and it is fixed.** Everything below was measured
before the cause was known and is left as a record of what ruling things out
looked like — every measurement in it is correct and none of it found the bug,
because every one of them launched a game into a FRESH PROCESS, which is the one
case that always worked.

**The lesson worth keeping: a headless test cannot reproduce a fault whose
trigger is the previous game.** `--launch` on a cold process was the wrong
instrument, used four times.

#### What was measured before the cause was found

MMagTech, during the UI session: two platforms *"launching with a black screen"*
and, a moment later, *"i hear audio"* — so the core is running and the picture is
not arriving. This is almost certainly the same fault as item 3b, which is
already recorded as real and not reproducible.

**WHAT WAS MEASURED, so nobody repeats it:**

- **3DO renders correctly HEADLESS.** `SDL_VIDEODRIVER=offscreen --launch 3068
  --frames 900 --render-size 1280x720 --screenshot` gives a frame whose extrema
  are (0,247) per channel. Not black, and the geometry is right: 320x240 at
  59.94 fps integer-scaled to a 1280x960 quad at 320,60 of the 1920x1080 canvas.
- **3DO renders correctly IN THE LIVE SESSION TOO**, on the same build, launched
  through `tools/ui-loop.sh --game 3068`. Ballz reaches its character-select
  screen and photographs cleanly.

So it is intermittent rather than per-platform, which is exactly what item 3b
says. **The two reported platforms are not written down here** because the
message said "msn snd 3d0" and only the 3DO half is certain — ask before
assuming which the other one was.

**Do not start from the frontend's draw path.** It was measured above and it is
correct. The next useful thing is a capture taken AT THE MOMENT it is black,
which the UI loop can now take without restarting the session: signal the
running frontend with `kill -USR1` and fetch `/tmp/cabinetos-frame.bmp`. A frame
that is black in that capture and a frame that is black only on the television
are two different faults — the second one is gamescope's.

## How the user wants this done

Plain answers. **Lead with what a change does and why it exists, in terms of
what breaks for the product — not in terms of the subsystem.** This was said
twice in one session and drifted back both times; a PR came back as "completely
foreign to me what it did and what it exists for", and a later explanation "went
way over my head". Keep the dense detail, but put it after the plain statement.

Check the running machine before theorising — most wrong turns come from
reasoning off an error message instead of looking. Say plainly what is verified
and what is assumed; they notice and ask. And they push back usefully: "are we
sure we can't do X?" and "you need to read Cabinet" both produced better answers
than the first one.

**Use the `MMagTech` handle, never the user's personal name** — not in files,
commits or documents, and the repo is public.
