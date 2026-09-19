# Picking this up

Written at the end of each session for whoever starts the next one, which is
usually a fresh assistant with no memory of what just happened.

**`docs/PROJECT.md` is the specification and it is authoritative. `docs/CABINET.md`
maps what Cabinet already solves.** This file is only the short version: what
state things are in, what to do next, and the handful of things that will waste
a day if nobody says them out loud.

Rewrite it at the end of a session. It is meant to be current, not a log.

---

## Before anything else

**Everything is on `main`.** No other branches and no open pull requests.
[#26](https://github.com/MMagTech/cabinetos/pull/26) merged and is the work
described below as "the image carries the console".

**THE HANDOVER GOES IN THE WORK'S OWN PULL REQUEST.** Write it inside the
branch that does the work it describes, so there is never a handover-only push
and never a handover-only pull request. The rule and the one narrow exception
are in PROJECT.md, *Constraints and principles*, item 7.

**THE A9 PRO IS INSTALLED AND RUNNING, 2026-09-19.** `cabinet@192.168.1.212`,
same SSH key as the VM, sudo password `cabinet`. It boots into the frontend on
**gamescope/drm** — the top compositor rung, which the VM has never reached —
rendering on its own Radeon 890M with **Vulkan present (RADV STRIX1)**, zero
session restarts, 1147 playable games. **Judge nothing about the look on it
until it is running an image with the resolution fix**; see item 1.

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
| **A9 Pro** | `cabinet@192.168.1.212` | the reference console. Real GPU, gamescope/drm, Vulkan. Judge the look here — and only here. |
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
- **Home is real**: a hero from RomM's own play history, Recent, Favorites.
- **Library, a grid, and a launch screen.** Every system including the ones this
  console cannot play, each saying why.
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
  Cabinet's own emulator strings.
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
  frontend's own GLES context, with no pixel read back anywhere.

## Pick up with these, in this order

### 1. Install the resolution fix on the A9 — DONE 2026-09-19

**The A9 is installed and running. One thing stands between it and the UI
work**, and it is in this change but not yet on the machine: the session
hardcoded `--output-width 1920 --output-height 1080`, written in Phase 2
before any display existed, and the panel is **3840x2160**. The console ran at
1080p and the television scaled it.

**That is the worst possible state for judging a look.** The frontend's shapes
are signed-distance fields, exact at any resolution and verified at all three
— so every soft edge on that screen was the scaler and none of it was the
design.

**Removing the flags does not fix it, and that was measured**: with no
`--output-width`/`--output-height` at all, gamescope still chose
`1920x1080@60Hz`. The session now reads the connected connector's preferred
mode from `/sys/class/drm/card*-*/modes` and passes it, with
`CABINETOS_OUTPUT=WxH` as an override.

**Done and verified the same day.** `bootc upgrade`, reboot, and the console
reports:

```
cabinetos-session: output 3840x2160 (from the display)
drm: selecting mode 3840x2160@60Hz
[frontend] GL_RENDERER AMD Radeon 890M Graphics (radeonsi, strix1, ACO)
```

A frame captured off it is 3840x2160, max pixel 255, 95% of the frame lit.
**The UI freeze is lifted** — everything under *Waiting on the reference
machine* is available, and what is on that television is now the real thing
rather than a scaled image.

**That same reboot found item 2**, which is a bigger problem than this was.

### 1c. An hour with a controller found five things — 2026-09-19

**Every one needed a person, a television and a pad. None was findable
headless.** Four are fixed; the fifth is item 3.

- **THE SOUND HAS NEVER WORKED.** `SDL_OpenAudioDeviceStream` was called only
  on the `--core` developer path, so a game launched from the library opened
  no audio device at all. It lasted a fortnight because every audio claim here
  was made by COUNTING SAMPLES out of `drainAudio()` — *"2,384 frames, zero
  audio"* is a sample count. **A headless VM has nothing to listen with.**
- **The shoulders, triggers and right stick were never mapped.** The pad sent
  a d-pad, four face buttons, Start and Select; RetroPad's other six inputs
  went nowhere. On Dreamcast the triggers are drive and reverse, so Crazy Taxi
  could not be played at all.
- **Start opened the overlay**, so no game could ever be paused. It is L3+R3
  now — Cabinet's default, chosen because trigger pairs collide with real
  gameplay and stick clicks mean nothing in anything we run. **Still owed:**
  Cabinet makes it remappable and GLOBAL rather than per-pad, with the second
  button clearable for single-button mode.
- **The overlay did not pause the game.** It ran on behind the menu.
- **Vertical arcade games render sideways.** Item 3.

**And one gap with no bug behind it: there is no interface sound at all.** The
only audio path in the program is the core's — no focus click, no selection
sound. tvOS gives Cabinet those for free; owning the whole stack means we get
nothing unless we build it. Belongs with motion and the glow: decidable only
in a room, with speakers.

**Pairing a controller works and has no UI.** `cabinet@192.168.1.212` has a
Switch Pro Controller paired and trusted (`E4:17:D8:71:F1:ED`), done entirely
with `bluetoothctl` over SSH. bluez, the MT7925 firmware, `hid_nintendo` and
SDL hotplug all work — `[frontend] gamepad connected` without a restart. What
does not exist is the screen, which is open question 15b. Worth knowing: that
scan turned up **seventeen devices**, sixteen of them neighbours' lights and
beacons — the concrete case behind "never auto-pair the first pad discovered".

### 1b. Installing the A9 — DONE 2026-09-19, and what it found

The install itself worked and the machine is described at the top of this
file. **Four faults turned up in the first hour on real hardware, none of
which the VM could ever have shown**, and all four are fixed in the same
change as the resolution one:

- **A fresh console threw its RomM token away.** `rommTokenPath()` is
  `$HOME/.config/cabinetos/romm.json` and nothing created that directory, so
  on a machine nobody has configured — which is exactly the machine that is
  pairing — the write failed after the person had already approved it in a
  browser. Worse, the line printed was `paired      (could not write ...)`: a
  success word with the failure in brackets. `saveToken` creates the parents
  at 0700 now, and the failure is loud and exits non-zero. **This would have
  hit every single person who ever installs CabinetOS.**
- **`--romm-probe` reported every platform as "not playable here"** on a
  machine with all twenty-one cores installed and working, because it returns
  before `catalog::setCoreDirectory`. Cosmetic, and corrosive: a diagnostic
  that lies is worse than one that says nothing.
- **The output was hardcoded to 1080p.** Item 1.
- **The installer is not fit for anyone but us.** Open question 5 is rewritten
  from "neither is tested" into what a person actually hits. The short version
  is in *Things that will bite you*.

**Two traps this project had already written down and I walked into anyway**,
so they are worth repeating:

- **`203/EXEC` is SELinux refusing to run a session script from the wrong
  place.** `build.sh` says so in as many words. A test copy in
  `/var/home/cabinet` will not exec; `chcon -t bin_t` fixes it, and `/usr/bin`
  gets the right label automatically. The machine flapped for two minutes
  until the drop-in came off — `Restart=always` with `StartLimitIntervalSec=0`
  did exactly what it was designed to do and the console recovered on its own.
- **`pkill -f <pattern>` kills the shell issuing it** when that shell's own
  command line contains the pattern. Third time on this project. `pkill -x`,
  or match on something the checker cannot contain.

**And the journal filter that wasted several minutes:** the session script's
output is tagged with the syslog identifier, so `journalctl -u
cabinetos-session` shows only systemd's own start/stop lines and none of the
console's output. Use **`journalctl -t cabinetos-session`**. Also, the clock
jumps when NTP syncs after install, so `--since` is unreliable on the first
boot — use `-n`.

### 2. WHAT TO DO NEXT, because the last session ended scattered

The A9 works. A person played on it for the first time and **five faults fell
out in an hour**, four fixed and one not. Read this order before picking
anything up — the list below it is longer than what actually matters now.

| | |
|---|---|
| **1** | **Vertical arcade games render sideways.** The one fault from that hour still open. Spec is complete — see item 3. Half the arcade library is affected. |
| **2** | **The first real in-game save.** Still not done: the pad only started working at the end of the session. Everything for it is now in place. |
| **3** | **Judge Home on the 65-inch.** The redesign is on the machine and has only been seen on a 27-inch desk monitor. |
| **4** | Then the offline work (item 4) or the core options (item 7). |

**Do not start new UI screens before 1 and 2.** Search and Settings are drawn
in the top bar and say "not built yet"; that is deliberate and can stay for
now.

### 3. Vertical arcade games render sideways — **NOT FIXED**

**`RETRO_ENVIRONMENT_SET_ROTATION` is in `libretro.h` and handled nowhere in
`core.cpp`.** DoDonPachi and every other TATE board renders sideways in its
framebuffer and asks the frontend to turn the picture; this console ignores
the request. Found by MMagTech on the A9, 2026-09-19.

**Cabinet does it and the whole design can be copied.**
`LibretroFrontend.mm`:

```objc
case RETRO_ENVIRONMENT_SET_ROTATION:
    // Vertical (TATE) boards render sideways in the framebuffer
    // and ask the frontend to rotate the picture. Value is in
    // 90-degree counter-clockwise steps.
    gRotation.store(*(const unsigned *)data, std::memory_order_relaxed);
    return true;
```

and it is applied in `NativePlayerRenderer.aspectFitVertices(textureSize:
viewSize:rotation:flipped:)` — **rotated texture coordinates in the quad, with
the source dimensions swapped when the rotation is odd.** Note it already
carries a `flipped` flag for GL frames that arrive bottom-row-first, which is
the same problem `core.frameUV()` solves here, so the two compose.

**Why it was not done in that session:** it is a RENDERER change, not a
mapping. `drawImageTexture` takes an axis-aligned quad with `u0,v0,u1,v1` and
a 90-degree turn cannot be expressed in those — it needs a rotation parameter
threaded through `image.cpp` and the renderer, and the letterbox and the glow
both reshaped, because a vertical game on a 16:9 panel is pillarboxed with a
TALL window rather than a wide one.

**One product question to settle first**, and it is MMagTech's: a vertical
game on a 4K 16:9 screen uses about a third of the width. Centre it at native
aspect with black either side, or integer-scale it as tall as the screen
allows? Nothing makes a vertical game fill a horizontal screen without lying.

### 4. The console dies if the server is away — **and every power cut hits it**

**Found by the A9's first cold boot, 2026-09-19.** The session came up faster
than the network, could not reach RomM, and the frontend exited. Because
gamescope exits when its primary child exits, the compositor ladder concluded
that *gamescope* had failed and permanently demoted the machine to **cage on
llvmpipe** — hardware rendering gone for the rest of the session, silently,
with nothing on screen saying so. It recovered only because somebody restarted
the service by hand.

Three faults, fix them together, and **open question 22 has the whole design**:

1. **Do not exit when the server is unreachable** — come up, keep retrying,
   fill in when it answers.
2. **Order the session after `network-online.target`**, so the race usually
   does not happen.
3. **The ladder must tell "the compositor failed" from "the app exited".** It
   exists for a machine with no usable GPU and must not be reachable by an
   application error.

And with it, the things that make an offline console useful rather than dead,
all specified in open question 22 with Cabinet's own rules quoted: **kept
games play with no server** (the library deliberately does not), a keep has to
**save the cover and a record** because our layout recovers the id and name
but not the art, saves **write to disk first and upload later** with a
four-rule precedence at launch, and **offline the console stays as the last
user it knew** and offers no switcher it cannot honour — MMagTech's call,
2026-09-19.

**The stand-in demo library must never appear on a console.** It is today's
fallback and it is worse than an error: it looks like a working console
showing somebody else's games.

### 5. One real in-game save, on Dreamcast

**No save in this class has ever been written by actually PLAYING a game here**,
and a headless VM cannot press Start. Every round trip so far restored a real
save, watched the core read it, and sent back byte-identical bytes — which is
the correct answer for a session that saved nothing, and is why forcing an
upload needed `--sync-test`.

MMagTech's call, 2026-09-19: *"real in-games will wait until we have the A9 in
hands and the OS written to it."* **Both of those are now true**, so this is
available as soon as a controller is plugged in — it does not even wait for
the resolution fix, because the test is a file reaching a server rather than
anything about the picture. Play a Dreamcast game, save in it, quit, and watch
the VMU reach RomM.

**`[frontend] gamepads at startup: 0` on the A9 as of 2026-09-19** — nothing
has been plugged in yet. Wired first: `bluez` and the MT7925's Bluetooth
firmware are both in the image, but USB is the bootstrap and the deterministic
one.

`frontend/src/filesave.{h,cpp}` is the mechanism and `catalog::saveFiles` is the
table.

### 6. The UI freeze lifts when the console is running on a television

**Decided 2026-09-17: no more UI is designed or tuned until CabinetOS is
installed on the reference machine.** The user's call. **The condition is the
console RUNNING THE FRONTEND on a television — not the box being unboxed, and
not the image being installed.**

**That is now true, with one asterisk: item 1.** The console is on a
television, on its own GPU, at the wrong resolution. Everything under *Waiting
on the reference machine* becomes available the moment it reports
`cabinetos-session: output 3840x2160`, in the order it is written.

**The line is the acceptance test, not the subsystem.** If the test is "does
this look right", it waits. If the test is a measurement or a behaviour, it
goes ahead — and a screen that already exists is not frozen, because fixing
something *wrong* is not the same as tuning something.

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
matters because portable save states are the premise the whole product rests on.
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

## Waiting on the reference machine, and deliberately not started

Ordered for whenever the console is running on a television. **Do not begin
these in the VM.**

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
- **First run**, which is now designed and not built — **open question 15b**,
  written with MMagTech on 2026-09-19 after setting the A9 up by hand over
  SSH. The requirement is one line and it is testable: **a keyboard is needed
  exactly once, ever.** A keyboard is the only input an installed machine
  guarantees, because the firmware boot menu needs one; a wired controller is
  not, because most pads sold now are Bluetooth. So setup runs on a keyboard,
  pairs a controller as its last step, and a second controller is added using
  only the first — with two-sided confirmation, so a neighbour's pad in
  pairing mode cannot answer for itself. **The mechanisms mostly exist** (the
  on-screen keyboard, the pairing flow's code and URL, `session.env`, bluez);
  what is missing is a state machine, a QR renderer, NetworkManager plumbing
  and a way to know it is the first run. **None of those is a picture**, so
  they can start before the look is settled.
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

### About the product

- **A truncated explanation is worse than none.** A tile's second line holds
  about sixteen characters beside a cover. Measure the column before writing the
  string.
- **Two tiles that read the same are one tile.**
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

## The answered questions people keep reopening

### PS3 STORAGE is answered. PS3 still cannot be PLAYED

**Read that twice**, because a heading saying "DONE" cost a conversation on
2026-09-19. What was measured is the STORAGE question and nothing else. Two
games were installed and booted far enough to prove the PKG could then be
deleted; **neither reached gameplay and neither could, because the test VM has
no GPU.** Playing a PS3 game needs a Vulkan path in the host, which is open
question 20 — **and the A9 Pro is the machine that makes it possible.**

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
- `~/cabinetos/.core-src/` — per-core checkouts, **4.8 GB, of which PPSSPP is
  3.4 GB**. They are a cache: delete any to make room and the next build
  re-clones.
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

## Two things the user wants discussed, each in its own session

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
