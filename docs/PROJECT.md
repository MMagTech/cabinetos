# CabinetOS

> **Read this file first.**
> This is the living specification for CabinetOS. Every session working on this
> repository should read it before touching anything else, and should update it
> when decisions are made, phases change status, or new uncertainty appears.
>
> **Starting a session?** `docs/NEXT-SESSION.md` is the short version — what
> state things are in, what to pick up, and the things that waste a day if
> nobody says them. This file is the specification and is authoritative; that
> one is the handover. Rewrite it at the end of a session.
>
> **`docs/CABINET.md` is the companion to this file.** Cabinet already ships on
> iOS, tvOS and macOS and has answered most of what CabinetOS runs into. Read it
> before designing anything, and read Cabinet's own source before inventing an
> answer — it is not checked out here, so clone it. Doing this piecemeal, a
> question at a time, produced several half-right answers that had to be
> corrected.
>
> Rules for maintaining this document:
> - When a phase is finished, change its status and say what actually shipped,
>   not what was planned.
> - When you are unsure about something, add it to **Open questions**. Do not
>   guess and do not quietly resolve an open question without saying so.
> - When a decision is reversed, record the reversal rather than editing history.

---

## Where the project is — 2026-09-20

**Phase 0 complete. Phase 1 complete. Phase 2 mostly done. Phase 3 well under
way and running. Phase 5 started early, the hardest question in it is answered,
and as of 2026-09-17 every one of the twenty-one libretro cores is built and can
be run.**

**AND IT IS INSTALLED ON THE REFERENCE MACHINE, 2026-09-19.** The GEEKOM A9
Pro boots into the frontend on `gamescope (drm)`, rendering on its own Radeon
890M with Vulkan present, zero session restarts, and the full 1147-game
library. See *The A9 Max, measured*. **The UI freeze is lifted**: the machine
runs at the panel's native 3840x2160, so what is on that television is the real
thing rather than a scaled image.

**AND VERTICAL ARCADE GAMES PLAY THE RIGHT WAY UP, 2026-09-19.** Half the
arcade library was on its side until then, because
`RETRO_ENVIRONMENT_SET_ROTATION` was handled nowhere. See *Vertical arcade
boards, and the turn they ask for* — it is a renderer change, it carries one
product decision of MMagTech's, and it contains the one trap that Cabinet had
already paid for.

**AND AS OF 2026-09-19 THEY ARE IN THE IMAGE, along with the frontend.** Until
that day the image was the OS half only: `cabinetos-session` ran
`sleep infinity` inside gamescope, so an installed machine booted to a black
screen, and everything a person would call the console lived on one development
VM and was compiled there by hand. Installing CabinetOS now installs CabinetOS.
See Phase 5, *The deploy*.

**AND A PERSON SAVED A GAME, 2026-09-19.** Somebody played Crazy Taxi 2 with a
controller, saved inside it, quit through the overlay, and the Dreamcast VMU
reached RomM — `updated 2026-09-20T02:48:19Z` on the row that already existed,
overwritten rather than duplicated. **No save in this class had ever been
written by actually playing a game here**; every round trip before it restored
a real card, watched the core read it, and sent back byte-identical bytes,
which is the right answer for a session that saved nothing. See
`docs/NEXT-SESSION.md` item 3.

It could not have happened a day earlier, and the reason is the one below about
the analogue trigger index: the Dreamcast's accelerator and brake are its
triggers, this console answered that channel with the right stick, and Crazy
Taxi could not be driven at all.

### The thing that matters most

**Save states are portable between Cabinet and CabinetOS.** Proved, not
reasoned about: Gambatte built for Linux x86-64 at the same commit Cabinet's
macOS build is pinned to, then each platform loading the other's state and
producing an identical digest — with a same-platform control run to make it a
result rather than a coincidence. The emulation is bit-identical across
architectures for twenty-five seconds of video and audio.

That is the premise the whole product rests on, and it was the largest unknown.
See Phase 3's notes and open question 13.

### What runs today

The frontend is a real program on the test VM, booted into by the session
rather than launched by hand:

- C++20, SDL3, one EGL/GLES 3 context. No toolkit. `frontend/`.
- The design system's focus treatment, motion, canvas and type ramp, verified
  identical at 4K, 1080p and 720p.
- Text (Noto Sans, with CJK fallback), cover art (async, budgeted, evicting),
  and **frosted glass**.
- **An on-screen keyboard**, which was the gate on everything downstream.
- **The library is reachable**, as of 2026-09-16: a Library of every system and
  every collection, a grid of each one's games, and a launch screen carrying
  Play and Download. Before it, 1100 playable games had fifty reachable.
- **A libretro core host** that loads a `.so`, paces it against the wall clock,
  plays its audio, draws its picture, and saves and restores its state.
  **Dr. Mario runs.** It also hosts the cores that draw for themselves: it owns
  the GLES context and hands a hardware-rendered core a framebuffer inside it,
  so **Mario Kart 64 and Ikaruga run too**, with no pixel ever read back.
- **An on-disk layout somebody can find their way around**, as of 2026-09-18:
  `roms/`, `cache/`, `bios/` and a directory per person holding their saves and
  states. Keeping a game is a decision per person rather than a flag on the
  game, and releasing the last one deletes the game and gives the space back.
  Open question 18.
- **The image carries the console**, as of 2026-09-19: the frontend at
  `/usr/bin/cabinetos-frontend`, the twenty-one cores at
  `/usr/lib/cabinetos/cores/` and PPSSPP's system files at
  `/usr/share/cabinetos/system/`. 273 MB. The image build calls
  `build-frontend.yml` and `build-core.yml` rather than repeating them, so
  **every image build now asserts all twenty-one pinned revisions**, and the
  last thing it does before shipping is ask whether the binary and all
  twenty-one cores can resolve every library they link against inside the image
  itself. Phase 5, *The deploy*.
- **Saves reach the server for every platform this console can play**, as of
  2026-09-19. The 47 of 81 rows on the reference server that could neither be
  uploaded nor restored — Dreamcast, Sega CD, both arcade emulators, 3DO, Neo
  Geo Pocket, DS — now travel, filed under the same rows an Apple TV already
  reads. Ikaruga says 「データファイルのロードに成功しました」 to a card that
  came off RomM. See *The save audit*.

### First run has its mechanisms, and no screens — 2026-09-20

**All four of the things open question 15b listed as missing are built**: a
state machine, a QR renderer, NetworkManager plumbing, and a way to know it is
the first run at all. None of them draws anything, which is why they could go
ahead of the look. **The screens still wait, like every other screen.**

The polkit rule open question 17 asked for ships with them, and the reference
console correctly reports that it is *already configured* and needs no setup —
which is the rule that matters most, because that machine was set up by hand and
must never be shown a wizard.

Read open question 15b for the whole of it, including the three faults the QR
encoder's verification found and the deadlock the state machine's rules check
found.

### Open against the frontend right now

- **Mupen64Plus save states do not restore the machine exactly**, and it is
  reproducible to the digit. Found 2026-09-16 the day N64 became runnable; it
  blocks nothing, because mupen64plus has no shared emulator tag, so its states
  never travel. What `--state-test` says on Mario Kart 64, twice, with
  byte-identical digests across two separate processes:

  - The picture is **moving**, so the comparison is meaningful — not the static
    title screen that once made this test report PASS and prove nothing.
  - **Restoring is self-consistent**: two restores produce identical video and
    identical audio.
  - **Both differ from the uninterrupted run**, from video frame 0 of 300, and
    the audio differs too — by four bytes of a million, which is one sample.

  Three candidate causes and **none of them is established**: the state may not
  capture everything; `retro_serialize` may perturb the core, since the run it
  is compared against is the one taken immediately *after* the save; or the
  picture may depend on graphics-plugin state that lives outside the state at
  all — though the audio differing argues against that last one on its own.

  **The instrument is not what is wrong, and that was checked rather than
  assumed.** The same test on the same build, on mGBA with a moving picture,
  reports video and audio MATCH and PASS. The failure is this core's.

- **A white line reported under the keyboard's title, not reproduced.** Every
  row between the title and the field was scanned in the captured framebuffer
  for a bright horizontal run and there is none; the strongest edge there is the
  field's own top boundary, which can only darken. So it is either a VNC scaling
  artefact or something the capture does not see. **Ask before chasing it.**
- **Horizontal wrapping is in, vertical is not.** Judge it with a pad.

### Decided 2026-09-17: the look waits for the reference machine

**No more UI is designed or tuned until CabinetOS is installed on the mini PC.**
The user's call, and it follows from what this document already records rather
than from a new argument: overscan eats more vertical room than a framebuffer
capture shows, motion on llvmpipe is tuned against the wrong feedback, and the
reference implementation's hero height needed four attempts on real hardware.
Building more screens against a software-rendered VM is building against a lie.

**The line is the acceptance test, not the subsystem.** Plenty of work has a
visible result and is still fair game:

- **If the test is "does this look right" — wait.** Layout, spacing, type sizes,
  motion, the letterbox glow, the safe area, the navigation bar, the boot
  splash. A capture cannot answer any of them and a VM cannot either.
- **If the test is a measurement or a behaviour — go.** Does the save reach
  RomM. Does the core report the pinned revision. Does the disk refuse the keep.
  Does the state load. Does the catalog give the right answer. These are decided
  by numbers and by running things, and the VM answers them as well as any
  television would.

A screen that already exists is not frozen — fixing something *wrong* is not the
same as tuning something. The rule is about adding and polishing.

**What this unblocks rather than blocks.** The list that survives it is long,
and it is the half of the project with the least guesswork in it: core options,
the last emulator, saves landing on the right triggers, the save class that does
not sync at all, BIOS detection, the N64 state divergence, storage that eviction
cannot see, and the two heavy systems. See `docs/NEXT-SESSION.md`, which is
ordered this way now.

**WHEN IT LIFTS, AND IT IS NOT WHEN THE MACHINE ARRIVES.** The freeze says
"until CabinetOS is installed on the reference machine", and the thing that
makes that sentence mean something changed on 2026-09-19: until then,
installing CabinetOS on it would have produced a black screen, because the
image contained no frontend. It contains one now. **The condition is the
console running the frontend on a television, not the box being unboxed and not
the image being installed.**

**IT IS NOW RUNNING THERE, AND THERE IS STILL ONE THING TO DO FIRST.** The
console came up on `gamescope (drm)` on its own GPU the same day — but at
1920x1080 on a 3840x2160 panel, because the session hardcoded the mode. Until
an image carrying that fix is installed, **anything judged on that screen is
judged through the television's scaler.** The fix ships in this change; verify
the console reports `cabinetos-session: output 3840x2160` before forming a
single opinion about the look.

### What is still unknown, honestly

- **Nothing has been judged on a television.** Motion, the letterbox glow and
  the safe area are all recorded as needing the reference machine — the A9 Max,
  which arrived AND WAS INSTALLED on 2026-09-19 and now runs the frontend on
  its own GPU. A software-rendered VM could never answer any of them; that
  machine can. They become answerable the moment it runs at the panel's native
  resolution rather than a scaled 1080p — see *The A9 Max, measured*.
- ~~**Twenty cores of twenty-one are built**~~ **— all twenty-one are, as of
  2026-09-17.** PPSSPP was the last, and it runs: Lumines reaches its attract
  demo in colour with sound, writes its memory-stick save, and quits back to
  Home. **1147 of 1644 games are playable**, the four extra being PSP. All four
  backend-sensitive cores are settled: pcsx_rearmed and melonDS take the
  recompiler and share Cabinet's tag, picodrive matches Cabinet's flags exactly,
  and Flycast is blocked on Cabinet's own unscripted edits rather than on
  anything here. PPSSPP's backend is not a build flag at all — see open
  question 13.
- ~~**Two of the twenty cannot be RUN here**~~ **— they can, as of 2026-09-16.**
  Flycast and Mupen64Plus render through a graphics context rather than handing
  back pixels, and the host now owns one and hands them a framebuffer inside
  it. Measured, not assumed: **Mario Kart 64 reaches its title screen and
  Ikaruga reaches its own**, both launched from the real library, both with
  sound, and Home draws correctly on the way back out. That is Dreamcast, Naomi
  and N64 — the 43 games that took the count from 1100 to 1143.
- **Every core builds in CI**, on a GitHub runner from a bare checkout, with the
  finished `.so` asserted to report the pinned revision as its own version
  string — see open question 13.
- ~~**No controller has ever been attached.**~~ **A Switch Pro Controller is
  paired over Bluetooth and has been played with**, 2026-09-19. It has to be
  woken by its own Home button — `bluetoothctl connect` fails with
  `br-connection-create-socket` on a sleeping pad — and there is still no
  pairing screen, which is open question 15b.
- **A GAME CAN DRAW NOTHING AND NOBODY KNOWS WHY.** Six FBNeo launches in one
  session drew only the letterbox glow while the core ran and made sound, and
  it has not reproduced since. The frame is known to reach the texture intact
  and to be lost at sampling; two theories were tested and both falsified. See
  `docs/NEXT-SESSION.md` item 3b, which also has the command that photographs
  the television with the session running.
- ~~**Whether a PS3 PKG install costs double the disk**~~ **— it does not, as of
  2026-09-18.** A PKG installs to the same size it came in at, so the 2x lasts
  only while both the PKG and the install exist. Measured on a 287 MB title and
  a 19.8 GB one, and both boot with the PKG deleted. **PS3 games can still not
  be PLAYED here** — that needs a GPU and Vulkan, and waits for the A9 Max. See
  open question 19, *The PKG install, MEASURED 2026-09-18*.
- **A decrypted ISO is a better shape where it is available** — one file, no
  install, no licence, and `beginLaunch`'s reuse test works on it unchanged.
  RPCS3 opens such an image itself, given a 20-byte disc header that ordinary
  ISO builders omit; the fix is on the server rather than in this console.
  **But it bounds out at six of the thirty titles**, because 24 are PSN PKGs
  with no disc behind them, so the install route is the majority case and still
  has to be built. See open question 19, *The better answer: a decrypted ISO*.
- **Three emulators now come from Flathub at pinned revisions**, installed on
  first boot rather than baked into the image — RPCS3, xemu and Eden. Baking
  them in looks like it works and does not: `/var` is unpacked only from the
  initial image, so the emulator would never move again. **Nothing launches them
  yet** — that is open question 12. See open question 21.

### The Cabinet-side debts this project has found

1. **Flycast carries unscripted edits** in its working tree, so its pin does not
   reproduce what ships — for Dreamcast and Naomi. Capture that diff before
   anything touches the tree.
2. **PPSSPP's manifest entry says `patches: null` and `build_args: null`, and
   both are wrong.** `tools/build-ppsspp.sh` applies two source patches — a
   shader-cache save on context loss, on every platform, and a CPU-engine probe
   on the Mac — and passes real CMake flags, of which `USING_GLES2` and
   `MOBILE_DEVICE` change what the binary is. The same shape as Flycast's thin
   patch inventory, and recoverable in the same way: by reading the builder
   rather than the manifest. **The manifest is not yet a complete description of
   how a core is built**, and it is load-bearing for parity.
3. **Eleven of twenty-three cores ship different revisions to iOS and macOS**,
   and eleven of the twenty-one tvOS revisions were recorded as unrecoverable.
   `core-manifest.json` pins each forward, which is right and far cheaper now,
   in alpha, than once anyone has a save history. **Two of the eleven have since
   been recovered straight out of the shipping archives** — see open question 13
   — and the same trick probably works on several more.
4. **mGBA's Mac archive reports `e31759b24-dirty`**, so that build carries a
   working-tree modification no script applies, in a core whose manifest entry
   lists no patches at all. Same shape as Flycast's, found the same way.
5. ~~**`core-manifest.json` is not pushed to GitHub.**~~ **It is, and has been
   since `37ca75d`** — `docs/core-manifest.json`, byte-identical to the copy in
   `~/Downloads` that this project has been reading. Checked 2026-09-17 by
   cloning Cabinet and diffing the two. Recorded because the opposite was
   written down here and in the handover, and a debt that is already paid is
   still a wrong fact about the project.
6. **`PS2PlayerView.swift`'s header says the screen has no pause menu and no
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

Running infrastructure:

| | |
|---|---|
| Repository | https://github.com/MMagTech/cabinetos |
| Image | `ghcr.io/mmagtech/cabinetos:latest` — public, unsigned |
| Test machine | Unraid VM at `192.168.1.250`, 4 GB, VirtIO-GPU, SSH key installed |
| Reference hardware | **GEEKOM A9 Max** — Ryzen AI 9 HX 370, Radeon 890M — **not yet installed**. Replaced the Beelink SER5 on 2026-09-17; see *Hardware*, where the rule is that everything keyed to "the SER5" now means this machine. |

Everything in the pipeline has run green at least once: image build → GHCR →
signing (skipped, no key) → qcow2 → VM boot → `bootc upgrade` in place →
installer ISO. The one untested link is **booting that ISO** and installing to
real hardware.

Known gaps, none blocking:

- **Unsigned images.** No `SIGNING_SECRET` set. Optional until Phase 7, when an
  installed machine needs to verify an update before rebooting into it.
- **SSH is on by default with password auth.** Deliberate for Phases 1–5, and a
  debt Phase 6 must pay — see open question 8.
- **The ISO has never been booted.**
- **HDMI-CEC is specified and protected but unverified.** Nobody on the project
  has an adapter.

---

## What CabinetOS is

CabinetOS is a console operating system for generic x86-64 hardware.

The user experience target is a PlayStation or an Xbox, not a Linux PC. You
press the power button, the frontend appears, and everything from that point on
is driven with a controller.

The user never sees:

- a desktop
- a terminal or console
- a package manager
- a file browser
- a Linux error message

### The trigger channel, and why answering it wrongly is worse than silence

**Found 2026-09-19 by MMagTech, who said the shoulder buttons did not work.**

libretro has a third analogue index, `RETRO_DEVICE_INDEX_ANALOG_BUTTON`, and it
is not a stick: its `id` is a joypad button id, so L2 is 12 and R2 is 13. A
callback that tests for the LEFT index and treats everything else as the right
stick answers *"how far is the left trigger pressed"* with **where the right
stick is sitting**. Two faults in one line — the triggers cannot be pressed,
and the right stick drives them.

**And it is worse than returning nothing.** Flycast reads the analogue value
first and falls back to the digital L2/R2 bit **only when that value is exactly
zero**. Cabinet answers this index with a plain `0` and therefore works, by
taking the fallback. A real pad's right stick rests a few hundred counts off
centre; that is not zero, so it reads as *"the trigger is very slightly
pressed"* and suppresses the fallback entirely. **A bug that needed stick drift
to appear.**

CabinetOS now carries a real analogue value rather than Cabinet's zero —
`PadState` has `leftTrigger` and `rightTrigger` — so a pad with sprung triggers
gives a Dreamcast a continuous throttle, and one with switches, which is what a
Switch Pro Controller's ZL and ZR are, gives it 0 or full.

#### A button that does nothing is usually correct

A RetroPad has sixteen inputs and a real machine has fewer, so a press that
does nothing is normally right and looks exactly like a fault. **The Dreamcast
controller has no shoulder BUTTONS at all** — its L and R are the analogue
triggers — so on a Switch Pro Controller the top shoulders are meant to be
silent and ZL/ZR are the triggers.

The console stopped leaving that to guesswork on 2026-09-19:
`RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS` was accepted and thrown away, and is
now printed once per game, including the half nobody prints:

```
[input] port 0: ... B=A, A=B, X=Y, Y=X, L2 (trigger)=L Trigger, R2 (trigger)=R Trigger, Start=Start
[input] port 0 does nothing in this game: Select, L (shoulder), R (shoulder), L3 (stick click), R3 (stick click)
```

It earned its keep immediately: Metal Slug X binds **L3 to Select**, so the
L3+R3 overlay chord sends one stray press on its way to opening — which follows
the rule already chosen, and is now visible rather than theoretical.

### The input model

**The controller is the primary input device, and the only one the design may
assume exists.**

Every screen, every flow, and every recoverable error state must be fully
navigable with a controller alone. Any state the machine can reach where a
controller is not sufficient to continue is a bug, not a limitation. This is the
single constraint that most shapes the frontend: it rules out designs that are
only tolerable with a pointer, and it is why text entry gets an on-screen
keyboard rather than a text field and a shrug.

**A keyboard and mouse are supported, but never required.** If one is plugged
in, it should work — typing a RomM server address, a Wi-Fi password or a search
query is genuinely faster on a keyboard, and there is no reason to refuse it.

The distinction that matters:

| | |
|---|---|
| **Required** | Controller. Everything must work with one. |
| **Supported** | Keyboard and mouse. A convenience, never a dependency. |
| **Never** | A flow that *only* works with a keyboard or mouse. |

Concretely: the on-screen keyboard is the baseline for all text entry and is
always reachable. A physical keyboard types into the same field when present.
Neither implementation may be the only one.

---

## Hardware

**The target is x86-64 PC hardware, not one specific machine.**

**The reference machine changed on 2026-09-17, which is exactly what this
section said would happen.** It is now a **GEEKOM A9 Max — AMD Ryzen AI 9 HX 370
(Zen 5, 12 cores) with Radeon 890M integrated graphics (RDNA 3.5, 16 CUs).**

> **It is the A9 MAX, not the A9 Pro.** Written down as "A9 Pro" in twenty
> places from 2026-09-17 until 2026-09-20, when the machine was asked rather
> than remembered: `/sys/class/dmi/id/product_name` says `A9 Max`, which is the
> manufacturer's own string for the board. Corrected everywhere, and noted here
> so nobody helpfully corrects it back. A GEEKOM A9 Pro is a different machine. It replaces the Beelink
SER5 (Ryzen 5, Vega), which was only ever the spare box that happened to be
available.

> *"If the project works out, the hardware may change, and CabinetOS must not
> have quietly grown dependencies on this particular box in the meantime."* —
> written here in Phase 1, and now cashed in. Nothing had to change to move
> machines, which is the point of having said it.

**What moves with it:**

- **The performance floor moves a long way up.** Phase 8 exists to tune PS2,
  GameCube, Dreamcast and Naomi "on Vega integrated graphics". That premise is
  gone. The floor is now Zen 5 with a Radeon 890M, and Phase 8's targets should be
  re-read rather than inherited.
- **Everything keyed to "the SER5" now means this machine** — most importantly
  the decision that no UI is designed or tuned until CabinetOS is installed on
  it. That gate did not move, but the thing it waits for did.
- **PS3 becomes a hardware question that is ANSWERED.** MMagTech, 2026-09-17:
  *"ive seen numerous videos on youtube of it running ps3 including god of war
  3."* RPCS3 is CPU-bound rather than GPU-bound — the work is emulating the
  Cell's SPUs — and Zen 5 carries AVX-512, which is the instruction set that
  matters most for it. **Treat PS3 performance as settled and stop revisiting
  it.** What remains for PS3 is architectural and storage-shaped, not
  performance-shaped; see open question 12.

**What does NOT move:**

- **Still AMD**, so the base image stays plain `bazzite` and open question 11
  (NVIDIA) stays out of scope. No second image to build, sign and boot.
- **Still no wired CEC pin.** Essentially no x86 mini PC has one, so the USB
  adapter stays in the bill of materials.
- **Storage is unaffected.** A faster CPU does not make PS3's 307 GB, or its
  37 GB single title, any smaller.

**One thing to verify rather than assume when it arrives:** Strix Point is recent
enough that its graphics support depends on a current Mesa and kernel. Bazzite
44 is Fedora 44 with Mesa 26.2, which should be comfortable — but "should be" is
the phrase this project has learned to distrust, so boot it before believing it.

The original text follows, because the reasoning is still the rule:

The development and reference machine was a Beelink SER5 mini PC (AMD Ryzen 5,
Radeon Vega integrated graphics) because that is the spare box available. It is
not the product's definition. If the project works out, the hardware may change,
and CabinetOS must not have quietly grown dependencies on this particular box in
the meantime.

What follows from that:

- **The reference machine sets the performance floor, not the ceiling.** Phase 8
  tunes PS2, GameCube, Dreamcast and Naomi to run acceptably on Vega integrated
  graphics. Anything faster is a bonus, and nothing may *require* a specific GPU.
- **No machine-specific quirks in the image.** No SER5 firmware workarounds, no
  hard-coded device paths, no assumptions about a particular audio or network
  chip. If the SER5 needs something unusual, that is a strong signal the fix
  belongs upstream in Bazzite, not here.
- **Hardware capability is discovered, not assumed** — with one exception, HDMI-CEC,
  which is a requirement rather than a capability. See below.
- **AMD for now.** Bazzite publishes NVIDIA variants of its images, so an NVIDIA
  machine is a base-image change rather than a rewrite — but it doubles the
  images to build and test, so it stays out of scope until there is hardware
  that needs it. See open question 11.

### HDMI-CEC is a hard requirement

**The console must be able to turn the television on, and be woken by it.** That
is not a nicety. It is a large part of what separates a console from a computer
sitting under the telly, and this product is defined by that difference.

Treat it like the controller: a thing the design may assume exists.

#### A USB adapter is required, on essentially any mini PC

**The Beelink SER5 has no wired CEC pin. Neither does the GMKtec K11.** Nor do
almost any x86 mini PCs — the HDMI connector carries the CEC line, but the board
does not wire it to anything. This is close to universal and should be planned
for rather than checked hopefully.

So the hardware requirement is a **USB CEC adapter**, and it is part of the
bill of materials for any CabinetOS machine.

| Adapter | Verdict |
|---|---|
| **Pulse-Eight** | The known good choice. Has a dedicated `inputattach` unit in the image. |
| RainShadow | Also has a unit present; untested here. |
| Ugreen | **Behaves inconsistently in native mode.** Avoid, or use legacy mode. |

#### Two modes, and they behave differently

Bazzite ships both CEC stacks, switched with `ujust cec-mode` or the Bazzite
Portal:

- **Legacy** — `libcec` and `cec-ctl`, driving the `cec-onboot`, `cec-onsleep`
  and `cec-onpoweroff` services. This is the mode for external USB adapters,
  and therefore **the mode that matters on x86**.
- **Native** — Valve's `linux-cec`/`cecd`, for devices with CEC wired into the
  kernel. **`cecd` is known to interfere with wakeup on HTPC setups using
  dongles**, which is exactly our configuration, so native is not the default
  to reach for here.

Native mode is also **incomplete in this image**: Bazzite's native mode enables
`steamos-manager-configure-cecd.service` alongside `cecd`, and `steamos-manager`
is `bazzite-deck`-only. Pulling it in would drag the SteamOS management layer
into the image, which constraint 4 rules out. Legacy adapter mode is the
supported path; this is recorded rather than fixed.

#### The packages are protected, by an assertion rather than a comment

Every package CEC needs looks like cruft in a package list — `v4l-utils` reads
as a webcam package, `linuxconsoletools` as a joystick utility — and both are
load-bearing. `build_files/require-cec.sh` names each one with its reason and
**fails the build** if any goes missing, whether by our hand or an upstream
change. Audited and confirmed present on the running image: `libcec`,
`v4l-utils`, `linux-cec`, `linuxconsoletools`, the four udev rules, and all
seven CEC systemd units.

#### CEC will not be tested by the author

**MMagTech is not buying a CEC adapter. Testing will come from other people.**
That is a fine arrangement and it has consequences worth stating plainly,
because they shape how the feature must be built:

- **The no-adapter path is the one that gets exercised daily**, on every machine
  the author owns. That is lucky — it is also the common case for anyone who
  has not bought an adapter yet — but it means the with-adapter path only ever
  runs on someone else's television.
- **CEC must never be able to break boot or the session.** A missing, unplugged
  or misbehaving adapter has to be a quiet no-op. If CEC code can hang startup
  when no adapter is present, the author will not see it; if it can hang startup
  when one *is* present, the author will not see that either.
- **The settings UI has to be self-explanatory**, because the person switching
  modes will be debugging their own television without the author watching. A
  mode switch that needs explaining is a mode switch that will be reported as
  broken.
- **There must be a way to get a useful report back.** Which adapter, which
  mode, which television, what happened. Developer mode's SSH is the obvious
  channel for anyone technical; for everyone else the UI should show enough
  state — adapter detected, mode, last CEC event — that a photograph of the
  screen is a useful bug report.
- **Be honest in the status.** A hard requirement nobody on the project can test
  is a hard requirement in name only until someone confirms it. Until then it is
  specified and implemented, not verified, and the phase notes should say so
  rather than implying otherwise.

#### CabinetOS must surface the mode switch itself

**CabinetOS removes the terminal, and with it `ujust` and the Bazzite Portal** —
both mechanisms Bazzite provides for choosing a CEC mode. Verified: `ujust` is
gone from the built image.

So mode selection has to exist in CabinetOS's own settings, or the choice is
unreachable on a finished machine. **Phase 7 task**, recorded there.

The reference implementation is `/usr/share/ublue-os/just/81-bazzite-fixes.just`,
which survives in the image even though `ujust` does not. It writes `CEC_MODE`
to `/etc/default/cec-control`, toggles the three legacy units against
`cecd.service`, handles the two `inputattach` templates, and writes a `cecd`
config with `logical_address = "playback"`, `suspend_tv = true` and
`allow_standby = false`.

---

## The backend model

**RomM is the single source of truth for the library.** CabinetOS is a client.

Everything comes from the RomM server over its REST API:

- games and ROM files
- artwork and metadata
- platform information
- firmware and BIOS files
- save files and save states

Authentication uses a **client API token** obtained through RomM's device
pairing flow, so that a machine with no keyboard can be authorised without
typing a password.

CabinetOS stores nothing the server does not already know, with two exceptions:

1. the local cache of downloaded games
2. emulator configuration

Games are downloaded on demand and cached locally, and can additionally be
**kept** — pinned to the internal drive so they are never re-fetched or evicted.
The user can see what is cached, what is kept, and how much space is free, and
can move games between the two. See *Emulation* for why the distinction matters.

Saves and save states sync back to the server, so a game started on Apple TV
can be continued on CabinetOS and vice versa. This bidirectional continuity is
the single most valuable feature of the product and should be treated as such
when trading off scope.

---

## The frontend

The frontend matches the existing Cabinet apps in visual language, navigation
feel and motion, so that the Apple TV app and CabinetOS read as one product
rather than two things that share a name.

Screens:

- **Home** — resume-first. What you were playing, continue it.
- **Browse** — by platform and by collection.
- **Game detail** — artwork and metadata.
- **In-game overlay** — reachable from a controller button without leaving the
  game. Offers save states, resume, and exit.
- **Settings** — account, storage, controllers, display, system update, and
  **About**, which carries the version and the credits (see *Branding*).
- **First run setup** — pairs with a RomM server. On-screen keyboard is the
  baseline; a physical keyboard types into the same field if one is attached.

The frontend is **owned, not skinned**. CabinetOS does not ship someone else's
frontend with a theme applied, because that makes the product's identity
hostage to an upstream project's design decisions.

---

## Reference implementations and upstreams

Future sessions should look here before inventing anything.

| What | Where | Why it matters |
|---|---|---|
| **Cabinet** (iOS/tvOS, Swift) | https://github.com/MMagTech/cabinet | **The reference implementation.** Source of truth for the frontend design language, for correct RomM client behaviour, and for how cores are hosted in-process. When in doubt about how a screen should look or how an API call should be made, read Cabinet. |
| Cabinet — `RommApp/RommApp/Native/` | same repo | The core inventory and the libretro frontend. Read `Libretro/LibretroFrontend.mm` before writing ours. |
| Cabinet — `RommApp/RommAppMac/` | same repo | The macOS app: the persistent-local-library half of the model CabinetOS is a hybrid of, and the only platform with Dolphin and PCSX2. |
| Cabinet — `tools/build-core.sh` et al | same repo | Per-platform core builds. CabinetOS adds a Linux target — open question 13. |
| Cabinet — `docs/native-in-game-saves.md` | same repo | How saves and save states work today. Phase 4 must match it. |
| Cabinet — `docs/core-quality-pass-2026-08-17.md` | same repo | Which cores are good and why they were chosen. |
| Cabinet — `RommAppTV/TVCoverFocus.swift` | same repo | **The three focus treatments, in 200 lines.** The single most useful file in the repository for Phase 3; its comments record why each system default was rejected. |
| Cabinet — `RommApp/RommApp/UI/TenFootMetrics.swift` | same repo | Every ten-foot size in one place. |
| Cabinet — `RommApp/RommApp/Native/NativeLauncher.swift` | same repo | The ROM's path from RomM into a running core, and the three directories. |
| Cabinet — `RommApp/RommApp/Native/NativePlayerRenderer.swift` | same repo | The frame loop, the accumulator and the audio governor. |
| Cabinet — `CLAUDE.md`, `ROADMAP.md`, `docs/settled.md` | same repo | Conventions and decisions already made. Read before proposing anything. Its **tvOS conventions** section is short and every line of it is a mistake already paid for. |
| **RomM** | https://github.com/rommapp/romm | The server. |
| RomM docs — Client API Tokens | https://docs.romm.app/latest/developers/client-api-tokens/ | Device pairing flow for keyboard-less auth. Phase 4. |
| RomM docs — Device Sync Protocol | https://docs.romm.app/latest/developers/device-sync-protocol/ | Wire format for syncing saves, states and play sessions. Phase 4. |
| RomM docs — API Reference | https://docs.romm.app/latest/developers/api-reference/ | REST endpoints. |
| RomM docs — WebSockets | https://docs.romm.app/latest/developers/websockets/ | Live update channels. |
| RomM docs — OpenAPI | https://docs.romm.app/latest/developers/openapi/ | Client codegen. |
| **Grout** (RomM first-party) | RomM ecosystem docs | RomM's own Linux handheld companion for muOS/NextUI. Does bidirectional ROM/save/state sync over Wi-Fi. Read it before writing our own sync layer — it may already define the behaviour we want. |
| **Bazzite** | https://github.com/ublue-os/bazzite | Our base image. |
| **ublue image-template** | https://github.com/ublue-os/image-template | Structure and conventions this repo follows. Our `Justfile`, `.github/workflows/` and `disk_config/` are derived from it (Apache-2.0). |

---

## The update model

**One version number for the whole system.**

The UI checks for a new release, shows a console-style update screen, pulls the
new image, and reboots into it. The user never updates a core, a package, a
dependency or a frontend separately.

There is no state in which the frontend and the system are on different
versions. This is the reason for building on a bootc/OSTree base: the entire OS
is a single signed, versioned artifact, and a bad update is a rollback rather
than a recovery USB stick.

**TRUE AS OF 2026-09-19 AND NOT BEFORE.** For a fortnight this was a
description of an intent: the image carried the OS and the session, and the
frontend and the twenty-one cores were not in it at all, so a machine that
pulled an update got half a system and nothing said so. The half it got was
the half nobody was watching. Phase 5, *The deploy*.

Corollary: Bazzite's own automatic updater (`uupd`) is disabled, because it
would create exactly the partial-version state this model exists to prevent.

---

## Emulation

Native emulators running locally. Not streaming, not browser-based.

- Controller mapping is configured per system, by us, not by the user.
- Games launch from the frontend and return to it cleanly, with no visible
  transition to a desktop, a console, or another application's UI.
- Target systems are everything currently emulated on the existing setup,
  including the heavy ones: **PS2, GameCube, Dreamcast and Naomi.**

The reference machine's Vega integrated graphics is the performance floor that
matters here. Phase 8 exists because those four systems will need tuning rather
than defaults on hardware of that class.

### How Cabinet does it today

**Cabinet on tvOS runs native cores in-process.** You select a game, Cabinet
pulls the ROM from RomM, and injects it into an emulator core running inside the
app. There is no handoff to a second application and no separate launcher step.

**This is the single most important fact about the frontend**, and it has a
consequence that the Phase 0 and Phase 3 toolkit decision must be made in full
knowledge of:

> The CabinetOS frontend is not a launcher. It is an emulator host with a UI on
> top — a program that owns the frame loop, loads cores as libraries, feeds them
> ROM data and controller input, and presents their output.

That is a substantially larger and more constrained program than a menu that
shells out to other binaries. It rules out toolkits that cannot cheaply embed a
C library and put its frames on screen at a stable 60fps. It is also what makes
the in-game overlay tractable: when you own the frame loop, drawing over it is
natural. See open question 12, which is about what happens for the systems where
in-process is not viable.

### Core parity is a hard constraint

**CabinetOS must use the same cores as Cabinet, at the same versions.**

Save states are core-specific. A state written by one core is not readable by a
different core for the same system, and frequently not by a different *version*
of the same core — libretro cores break their own state format between releases
routinely. Since the entire point of the RomM sync layer is that a game started
on Apple TV continues on CabinetOS, a core mismatch silently destroys the
product's best feature. The game will boot; the save state will not load.

So core selection is not a CabinetOS decision. It is inherited.

What follows:

1. **Cores are bundled in the image and pinned**, never pulled from a package
   manager that can update them independently. This is consistent with the
   update model: one version number for the whole system, and cores are part of
   that version.
2. **Core versions move in lockstep across all Cabinet platforms.** Bumping a
   core is a coordinated release, not a CabinetOS-local change.
3. **There is no shared source of truth for core revisions yet, and there needs
   to be.** `tools/build-core.sh` clones each core's upstream repository with
   `git clone --depth 1` and builds whatever `HEAD` happened to be that day. No
   commit is recorded. The committed `.a` archives are the only artifact, and
   nothing says what source produced them.

   That makes core parity currently unachievable by construction: CabinetOS
   cannot build "the same revision" because the revision was never written down.
   It is also a latent problem inside Cabinet itself — rebuilding one core
   months after another means the two platforms carry different revisions of it.

   The fix is small and it belongs in Cabinet: a **core manifest** recording,
   per core, the upstream repository and an exact commit SHA. See open
   question 13.

   (`tools/generate_cores_map.py` is a different thing despite the name — it
   maps RomM platforms to *EmulatorJS* cores, generated from RomM's frontend
   source. Not a native core version manifest.)

### What Cabinet actually ships

Read in Phase 0, from the source rather than the tree.

Cabinet is its own **libretro frontend** — `Native/Libretro/LibretroFrontend.mm`,
`LibretroCoreAPI.h`, `libretro.h`, plus Metal shaders. Cores are compiled to
static archives per platform and linked in: `libflycast_ios.a`,
`libflycast_tvos.a`, `libflycast_mac.a` and so on, built by `tools/build-core.sh`
and per-core scripts.

**23 cores, not 25** — this document previously counted the `Libretro` and
`Archive` directories, which are the frontend and a vendored 7-Zip/zlib, not
cores. The 23:

> BeetleNGP, BeetlePCEFast, BeetleVB, FBNeo, FCEUmm, Flycast, GW, Gambatte,
> GenesisPlusGX, MAME2003Plus, MGBA, MelonDS, Mupen64Plus, Opera, PCSXReARMed,
> PPSSPP, PicoDrive, ProSystem, Saturn (Beetle), Snes9x, Stella2014, VeMUlator,
> Vecx

Two of them — **GW and VeMUlator — are iOS-only by decision**, not by
limitation: tiny canvases that belong in a hand rather than on a television.
Both have `_ios.a` and `_mac.a` and no `_tvos.a`. CabinetOS should inherit that
decision and carry **21 cores**, not 23.

#### Cores and platforms are not the same list

Read from `NativeCore.swift` 2026-09-13, after this document got it wrong once.
**26 platforms, 22 cores**, and the mapping runs both ways:

| Core | Platforms it serves | |
|---|---|---|
| **Genesis Plus GX** | Genesis, Sega CD, Master System, Game Gear | **4 for one build** |
| **Gambatte** | Game Boy, Game Boy Color | 2 |
| **Beetle PCE Fast** | TurboGrafx-16, TurboGrafx-CD | 2 |
| **FBNeo** *and* **MAME 2003-Plus** | Arcade | **2 cores, 1 platform** |
| every other core | one platform each | |

Note what PicoDrive does *not* cover: it is Sega 32X alone. Genesis, Master
System and Game Gear are Genesis Plus GX. Reading the core list as a platform
list gets that backwards.

Three consequences, and the third shapes the code:

1. **One build can light up four platforms.** Genesis Plus GX is the best value
   per build in the set, which matters when ordering Phase 5.
2. **Wiring a core once does not mean every platform under it works.** The
   reference implementation's own rule, learned by losing saves: *wire it per
   core, confirm it per platform.* Genesis Plus GX exposes cartridge save RAM
   through the standard call for Genesis, Master System and Game Gear — but Sega
   CD's internal backup RAM is a separate file the core writes itself. Beetle
   PCE Fast does something for CD games and nothing at all for HuCards, which
   have no save hardware.
3. **Configuration is keyed by PLATFORM, not by core.** Verified:
   `NativeCoreOptionsStore.dictionary(for: platform)`,
   `padDevice(for: platform)`, `platform.supportsSecondPlayer`. The same core
   binary gets a different option table and a different controller device type
   depending on which system it is being asked to be — Sega CD forces
   `cart_size`, Saturn forces its save method, 32X gets its own pad type.

   **CabinetOS must key its own configuration the same way.** A
   `core -> settings` map would be wrong by construction, and expensive to
   unpick once a settings UI exists on top of it.

So the Phase 5 target is **21 libretro cores plus Dolphin and PCSX2 — 23 builds
— covering 27 platforms.**

#### And only three of them need a GPU

Also verified from the frontend's own source: exactly **three cores ever ask for
a graphics context** via `RETRO_ENVIRONMENT_SET_HW_RENDER` — **Flycast**,
**Mupen64Plus** and **PPSSPP**. The frontend's own comment calls the rest "the
twelve software-rendered cores", counting the set it had at the time.

Everything else hands over a finished pixel buffer, including several that look
like they should not: melonDS does its 3D on the CPU with a threaded rasteriser,
Opera is fully software, and vecx is deliberately built with its GLES path
compiled out (`HAS_GPU=0`).

That is why the first core to build is a software one: it needs no hardware
render callback, no shared context, no FBO, and no readback. Those exist only
for three cores and can wait until one of them is the target.

And, **macOS only**, the two heavy ones — also as static archives, also
in-process:

> `RommAppMac/Dolphin/libdolphin_mac.a`, `RommAppMac/PCSX2/libpcsx2_mac.a`,
> built by `tools/build-dolphin-mac.sh` and `tools/build-pcsx2-mac.sh` with
> `tools/patch-dolphin-mac.py` and `tools/patch-pcsx2-mac.py`.

Dreamcast and Naomi are Flycast, which is present on every platform already.

### CabinetOS is a hybrid of the two Cabinet apps

| | Cabinet tvOS | Cabinet macOS | CabinetOS |
|---|---|---|---|
| Library | RomM, pulled on demand | local | RomM, pulled on demand |
| ROM storage | transient | persistent local | **both** |

CabinetOS takes the tvOS model — RomM as the source of truth, games pulled when
you want them — and adds the macOS model's persistence: a game can be **kept**
on the internal drive instead of re-fetched every time.

**Where** games live is the user's choice, not a fixed path. A console with a
small system drive and a big second drive is the normal shape, and a USB drive
should work too. Settings offers a storage location; the rest of the system
follows it.

That has a consequence for anyone building Phase 4: **do not hard-code the game
storage path.** It is configuration from the first line of code. Retrofitting
multiple locations into something that assumed one is expensive; designing for
it now costs nothing. See open question 14 for what still has to be decided.

Those are two different things and the UI must treat them as such:

- **Cached** — a side effect of playing something. Evictable without asking.
  The system may reclaim it when space runs low.
- **Kept** — a deliberate choice by the user. Never evicted automatically.
  Survives regardless of free space, and if space runs out the system says so
  rather than quietly deleting a game someone asked it to hold.

The Settings storage screen shows both, and lets a cached game be promoted to
kept and a kept game released back to cached.

**And keeping is an action on the GAME, not only a row in Settings.** Added
2026-09-16 at MMagTech's prompt, and it matches what Cabinet already ships — a
per-game toggle, with the size shown, removable from the same place it was
added. Settings is where you go to see the whole picture; the game's own screen
is where the decision is actually made.

**It has to work on a game that has never been played**, which is the case that
matters most and the one a promote-from-cache model misses entirely: browsing
the library, picking something for later, and having it there when you come
back. On a game already in the cache it pins what is there; on one that is not,
it is a download that stays. Cabinet calls this *Keep on device*; the word on
the button here should be whichever of **Download** or **Keep** reads better on
a television, and that is a Phase 4 wording decision rather than a design one.

**Kept games are what shrinks the cache**, since the cache is simply whatever
space is left over — see the cache policy in Phase 4.

---

## How Cabinet hosts cores

Read from the source 2026-09-13. This is the Phase 0 deliverable that matters
as much as the colour palette, because it is what decides the toolkit.

### The shape: one C++ frontend, cores as data

`LibretroFrontend` is a **process-global singleton**, deliberately. Libretro's
callbacks are plain C function pointers with no context argument, so there is
exactly one frontend's worth of state no matter how the wrapper is shaped, and
exactly one core active at a time. Cores are not re-entrant and the app never
runs two games at once.

Each core is described to the frontend by a **`LibretroCoreAPI` struct** — 21
function pointers covering the libretro entry points the frontend actually uses
(`init`, `load_game`, `run`, `serialize`, `get_memory_data`, and so on). The
frontend never names a core's symbols. That indirection exists purely because
only one core can carry the standard `retro_*` names when they are all statically
linked into one binary, so every other core's archive gets its symbols renamed
with a per-core prefix first.

**On Linux that entire mechanism is unnecessary.** One `.so` per core, `dlopen`ed
with `RTLD_LOCAL`, gives namespace isolation for free. The struct stays — it is a
good shape — but it gets filled by `dlsym` instead of by a generated wiring file.
See open question 13.

### A ROM's journey from RomM to a running core

`NativeLauncher.prepare(rom:session:)`, in order:

1. **Resolve the platform and core.** RomM's platform slug is canonicalised,
   mapped to a `NativePlatform`, which names one core (or several, where the
   user gets a picker — arcade only).
2. **Reject formats the core cannot take.** Saturn, PS1 and Dreamcast are
   `.chd`-only and single-file, deliberately, matching RomM's own recommended
   format for CD platforms. A multi-file cue/bin is refused with an explanation
   rather than attempted.
3. **Find the bytes.** Three tiers, checked in this order:
   - a **kept** game's directory (iOS/Mac), which already holds ROM and
     firmware — zero network;
   - a **cache** directory keyed by rom id (tvOS), left in place between
     launches so a replay costs no download;
   - otherwise **download** the ROM and *every* firmware file the platform
     lists. Not the one the board needs — all of them. A core looks BIOS files
     up by name in the system directory and ignores what it does not want, so
     extra files are harmless and a missing one is the only failure that
     matters.
4. **Extract if archived** (the vendored 7-Zip/zlib in `Native/Archive`).
5. **Restore saves before boot**, not after: any core-written save file is
   placed at the exact name the core will look for, which is the loaded
   content's basename.
6. **Activate the core**, apply core options, set the controller port device
   type, then `loadGame(romPath, systemDirectory:, saveDirectory:)`.

Three directories, and they are three different things — a lesson Cabinet
learned by losing saves:

| | What it is | Lifetime |
|---|---|---|
| **work directory** | where the ROM and firmware sit | per launch, or the kept/cache directory |
| **system directory** | where the core looks up BIOS by name | usually the work directory; the app bundle for PSP, whose "firmware" ships with the app |
| **save directory** | where a core writes its own save files | **must outlive the session** — `CoreSaves/<rom id>/`, namespaced by core on multi-core platforms |

**Pointing the save directory at the per-launch temp directory is how those
saves used to vanish.** CabinetOS must not repeat it: the save directory is
persistent storage from the first line of code, and on a multi-core platform it
is namespaced by core so two arcade emulators cannot overwrite each other's
NVRAM.

### Saves and save states are two different mechanisms

**In-game saves** (the cartridge battery, the memory card) arrive two ways, and
a core uses one or the other:

- `RETRO_MEMORY_SAVE_RAM` — the frontend reads and writes the core's buffer
  directly. Most cores.
- **A real file the core writes itself** into the save directory. Neo Geo
  Pocket, Sega CD, Dreamcast's VMU, FBNeo's NVRAM, melonDS's `.sav`.

The file-writing class has a sharp edge: **`retro_unload_game` is the one moment
those cores flush.** Cabinet's `unloadGame` exists precisely to force it at quit,
in RetroArch's own order — SRAM save, then unload, then deinit. A session killed
by the OS loses everything since launch on those platforms. CabinetOS owns its
own shutdown, so it can do better here than Cabinet can.

Three sizing traps, all confirmed on hardware, all of which CabinetOS inherits:

- **mGBA** reports a placeholder 128 KB until it has autodetected the save type,
  then re-initialises its buffer. Poll the size; a restore seated before that
  moment has to be applied again.
- **Genesis Plus GX** trims its reported size once the game is running.
- **Restore copies the smaller of blob and region** rather than demanding an
  exact match, because of the two above.
- **Game Boy's real-time clock is a separate region** (`RETRO_MEMORY_RTC`).
  Saving only the save RAM silently loses the clock Pokémon Gold and Silver
  depend on. It travels as its own `.rtc` file.

**Save states** are `retro_serialize`/`retro_unserialize`, taken on the thread
that drives `runFrame` — a snapshot taken mid-`retro_run` is corrupt by
definition. They are written only when the user picks Save state; nothing
autosaves them. Two cores (GW, VeMUlator) cannot serialize at all and their slot
UI hides.

### Where a save state lives, and how it reaches RomM

Local first, always. A kept game's state is written into a **`pending-states`
queue** inside its own directory before any upload is attempted, so losing
signal mid-save never loses the save.

The filename is the conflict resolution. Each state is
`<rom basename> [<ISO timestamp>]` with colons, dots and `T` flattened — RomM's
own naming — so an upload lands exactly as if it had happened online and can
never overwrite anything. **Syncing is only "finish the uploads."**

Upload is `POST /api/states?rom_id=&emulator=`, multipart, with the state and an
optional screenshot captured on the paused frame.

> **The `emulator` tag is the only thing standing between a good state and a
> corrupt one, and it does not carry a version.** It is a bare slug —
> `flycast-native`, `mgba-native`, `pcsx-rearmed-native` — and the launch screen
> uses it to grey out states the running core cannot restore. A CabinetOS build
> of a *different revision* of the same core would upload under the same tag, and
> Cabinet would offer the state as loadable. This is the failure mode *Core
> parity is a hard constraint* describes, and the tag cannot detect it. Either
> the builds are genuinely identical, or the tag has to grow a build identity.
> See open question 13.

### The frame loop, and why it is not trivial

This is the part that decides the toolkit, so it is worth stating exactly.

The renderer is driven by a **display link at the panel's refresh rate**, which
is *not* the rate the core wants. An NTSC core asks for 59.94; an Apple TV's
display link can run far above that. So each draw:

1. Accumulates wall-clock time against `1 / core.targetFPS`.
2. Runs `retro_run` while the accumulator has a whole interval in it, **at most
   twice per draw**, so a stall cannot bank a debt and repay it as a stutter.
   The accumulator itself is capped at four intervals — time beyond that is
   simply gone.
3. Presents the latest frame whether or not a core frame was produced, so the
   picture holds steady while the core is not yet due.

Running the core once per display tick instead was measured wrong on hardware:
Dreamcast produced 65,000–85,000 audio frames a second against 44,100 of
realtime, ~1.5× too fast, with the surplus discarded — which is what made music
play back sped up.

**One core needs a second brake.** Flycast's threaded renderer free-runs its
emulation thread as far ahead as its render queue allows — measured at up to
five times realtime. RetroArch's backpressure is a blocking audio callback;
Cabinet's callback must never block (it feeds a realtime ring), so the brake is
an **audio governor**: when the core's own audio output is ahead of the wall
clock by more than a 20 ms cushion, it is not due, whatever the accumulator says.
Skipping the run leaves the render queue unconsumed, which is what actually
stalls the emulation thread.

The cushion is felt latency: the lead the governor permits *is* input lag, at
10 ms per hundredth of a second. It was 50 ms, was reported as bad input lag on
Dreamcast, and is now 20 ms. **This is a number CabinetOS will have to tune
again**, because the display path is different.

Applied to every core the governor made things worse (it slowed N64 down). It is
Flycast-only, by measurement.

### Video: two paths, and one of them is free on Linux

**Software-rendered cores** (most of them) hand the frontend a pixel buffer in
one of three libretro formats — RGB1555, XRGB8888, RGB565 — which is copied out
per frame because cores reuse their buffer.

**Hardware-rendered cores** (Flycast, Mupen64Plus, PPSSPP) ask for a GL context
via `RETRO_ENVIRONMENT_SET_HW_RENDER` and render into an FBO the frontend owns.
The frontend then has to get those pixels *back* to draw them, and on Apple that
means `glReadPixels` into a pixel-pack double buffer, publishing the previous
frame while the current one copies. It costs a frame of latency and was worth it:
the synchronous version measured 12.7 ms inside `glReadPixels` on a heavy
Dreamcast scene — two thirds of the whole frame.

> **On Linux that readback should not exist.** It is there because the core
> renders in GL and the display path is Metal, so the pixels must cross an API
> boundary on the CPU. A Linux frontend that draws with GL or Vulkan can sample
> the core's FBO texture directly. This is a performance *gain* from porting,
> not a cost — and it removes the single largest per-frame cost the Apple build
> has on its three heaviest cores.

**BUILT 2026-09-16, and it does not exist.** The host accepts
`SET_HW_RENDER`, creates the target itself, and the player samples that texture
in the same context the core drew it in. Nothing is copied and nothing is read
back. `Core::frameUV` is where the two paths meet: a software core answers
"the whole texture, the right way up" and a hardware core answers a corner of a
larger target with its rows the other way round, so no caller above it knows
which kind of core is running.

Five things were not obvious in advance and each one would have looked like a
broken game. The fifth is PPSSPP's and arrived a day later, which is the point
of the list: each one is a different core teaching the same lesson.

- **The target is sized to the core's declared MAXIMUM, not its picture.**
  Flycast asks for 853x853 and then presents 640x480 into the corner of it.
  Sampling the whole texture draws a small picture in a large black field.
- **The picture is upside down**, because GL renders bottom-left origin and
  every software core hands back a top-down buffer. The core states which
  convention it used; it is read rather than guessed.
- **`retro_run` does not give the context back as it found it.** A core
  emulating a 3D machine leaves depth testing, culling, scissoring, a stencil
  mask and its own program bound. The UI's `beginFrame` establishes only what
  it uses, which was correct while nothing else touched the context. The host
  now restores the context after every `retro_run`, next to the thing that
  breaks it rather than in the renderer.
- **Integer scaling is wrong for these cores.** A Game Boy's pixels were each
  chosen by somebody; a Dreamcast's output is a 3D scene rendered at whatever
  internal resolution the core was asked for. At 3x internal resolution the
  frame is 1920x1440, flooring to an integer scale gives zero, clamps to one,
  and draws 360 rows off the bottom of the screen.
- **THE FRAME'S ALPHA IS NOT A COMPOSITING INSTRUCTION**, and taking it as one
  made PPSSPP look like a core that renders black. The player drew the game's
  texture with ordinary alpha blending, which is right for a cover and wrong for
  a picture: the alpha channel of an emulated machine's framebuffer is the
  machine's own state. Lumines leaves it at nearly zero, so the whole 1920x1080
  capture peaked at RGB **(4,4,4)** — the picture was there the entire time, at
  1.5% brightness, with text faintly legible against black. **The game surface
  is now drawn opaque for every core**, because it is a picture in all of them.

  The thing worth carrying is how it presented. The capture was not empty, so it
  did not read as "no frame"; it read as a plausible, nearly-black rendering,
  and the first instinct was to believe the core. Reading the actual pixel
  maximum out of the BMP took a minute and turned a guess into a number.

**Only GLES is accepted, and the version is read rather than assumed.** SDL is
asked for GLES 3.0 and the driver is free to hand back more — the test VM
returns 3.2 — so refusing a core that wants 3.1 on the basis of what was asked
for would be turning down something the machine can do. Desktop GL and Vulkan
are refused by name, because accepting and then failing inside the core reads as
a broken game rather than as a frontend that cannot do something. Flycast and
Mupen64Plus ask for GLES 3.0 and get it; **PPSSPP asks for GLES 2.0**, which
this GLES 3 context serves, and it only asks for GLES at all because it is built
with `USING_GLES2` — without it the same core asks for desktop GL and is refused
by name. See open question 13.

### Vertical arcade boards, and the turn they ask for

**BUILT 2026-09-19.** A vertical (TATE) board had its monitor bolted into the
cabinet turned ninety degrees, so it renders a sideways picture and asks the
frontend to turn it round with `RETRO_ENVIRONMENT_SET_ROTATION` — 0 to 3, in
90-degree counter-clockwise steps. This console ignored the ask, and **every
TATE game in a 223-game arcade library played on its side** for as long as
arcade has worked here. Found by MMagTech on the A9 with a pad, which is the
only way it could have been found: a headless capture is a picture nobody looks
at.

**ONLY AN ARCADE CORE EVER ASKS.** MMagTech's point, 2026-09-19, and it is what
makes the rule below safe: a console was built to put its picture on a
television the right way up, so no console core rotates. Every decision here is
therefore about arcade boards alone and cannot reach anything else.

**The turn cannot live in the texture coordinates**, which is why this is a
renderer change and not a mapping. `drawImageTexture` takes an axis-aligned
`u0,v0,u1,v1`, and no ordering of four numbers transposes x and y — swapping
them flips a picture, it never turns one. So the turn is applied to the quad's
**corner** in the vertex shader, before the corner is used to look up a texture
coordinate, and the uv rectangle goes on doing its own job.

**THE ORDER IS DELIBERATE AND IT IS NOT CABINET'S.** Cabinet rotates the
texture coordinates *after* flipping them, and `aspectFitVertices` says in as
many words that the two "never combine today" — its rotations are arcade boards
and its flipped frames come from the two GL cores, which do not rotate. That is
not safe here, so this frontend applies the turn first, in the picture's own
space, and lets the uv rectangle map the result into memory. Read it as: the
rotation says which part of the PICTURE a corner shows, and the uv says where
that part of the picture lives. Verified by forcing a turn onto a Dreamcast
frame, which arrives bottom-row-first: the picture comes out turned and not
mirrored.

#### The declared aspect of a turned board is ALREADY turned

The one trap, and Cabinet paid for it first. FBNeo reports `aspect 0.7500` for
DoDonPachi DaiOuJou — the 3:4 of the cabinet's tube on its side — while handing
back a **448x224** framebuffer. Inverting the declared value applies the turn
twice; Cabinet's own comment records the result as having *"stretched every
vertical game"*.

**So a turned picture takes its shape from raw pixels and ignores the declared
aspect.** Nothing else changes, and nothing else can: the platforms whose pixels
are not square — Saturn is the one that is unplayable without the declared value
— never rotate.

#### Two sizes a core reports, and neither is wrong

MAME 2003-Plus declares **224x256** in `av_info` for Arkanoid, which is the
picture as SHOWN, already turned, and then hands back **256x224** from
`video_refresh` every frame, which is the board's own sideways output. **The
layout must use the second.** One log line now prints both rather than leaving
it to be inferred:

```
[core] the core hands back 256x224 and asks for 270 degrees counter-clockwise; shown as 224x256
```

#### A turned picture fills the height

**MMagTech's call, 2026-09-19**, asked because nothing makes a vertical game
fill a horizontal screen without lying and the two honest answers differ. A
turned picture is scaled to its true shape until it is as tall as the screen
allows — on the A9's 3840x2160 panel that is **1080x2160, 28% of the width** —
rather than integer-scaled. Integer scaling would give 6x and throw away 11% of
the height on top of pillarboxing that is already unavoidable, and the
deliberate-dot-grid argument that earns a Game Boy integer scaling is worth
less than a third of the screen. **Every upright game keeps integer scaling
exactly as before.**

**The letterbox glow reshapes itself for free**, which is worth saying because
it looks like it should need work. Its shader ramps from the picture's edge to
the screen's in each direction separately, so a tall rect lights two wide bars
at the sides and nothing above or below. Measured on the A9: 8 at the picture's
edge falling to 0 at the panel edge. The rect is the whole interface.

#### What was measured, on the machines

| | |
|---|---|
| DoDonPachi DaiOuJou, FBNeo, 90 CCW | upright; 540x1080 at 1080p and **1080x2160 on the A9's own Radeon at 3840x2160** |
| Arkanoid, MAME 2003-Plus, 270 CCW | upright. A different emulator and the other odd turn |
| Metal Slug X, FBNeo | no rotation, unchanged, still integer scaled. **The control** |
| Ikaruga on Flycast, turn forced in | turned, not mirrored, on a bottom-up hardware frame |

**Flycast calls `SET_ROTATION(0)` explicitly**, so a core asking for no turn is
ordinary and will overwrite anything set before it. That is why the reset lives
in `loadGame` beside the other per-game state and not in `load`.

**Rotation is not read from a DAT, and Cabinet does not either.** Checked
2026-09-19 because it was raised as a likely memory. Cabinet ships three
MAME-derived JSON files under `Resources/ArcadeProfiles/`, and every field in
them is a CONTROL PANEL — `rotary`, `dial`, `trackball`, `pedals`, `lightgun`,
`paddle`. Which inputs a cabinet had, never which way its monitor faced.
`gRotation` has exactly one writer in Cabinet: the environment callback.

### Shaders, and the glow around the picture

Missed on the first Phase 0 read and added 2026-09-13 at MMagTech's prompt. Not
needed to get a core running, and very much part of what the product looks like.

**Eleven shaders**, one Metal fragment function each, with one pipeline built
per shader at attach — *"picking a shader in the pause menu is a dictionary
lookup, not a recompile."*

| | |
|---|---|
| `sharp` | "None" — unfiltered, the default |
| `sabr` | a scaler, offered everywhere |
| `crtAperture`, `crtEasymode`, `crtMattias`, `crtBeam`, `crtCaligari`, `crtGeom` | six CRT looks |
| `lcd` | a generic LCD grid |
| `gameBoy` | the dot-matrix look, built for that specific screen |
| `vmuLCD` | offered in **no** menu; set directly by the VMU player |

**They are gated per platform, and that gating is the interesting part.** The CRT
shaders simulate a television, and a Game Boy was never displayed on one — so
handhelds (GB, GBC, GBA, Game Gear, NGPC, DS) drop all six CRTs and get one
real-screen shader instead. It cuts both ways: consoles drop the handheld
shaders, *"since a PS1 game offering a Game Boy dot-matrix was the same mismatch
in the other direction."*

**And the choice is stored per PLATFORM, not per core** — with the source calling
out exactly the bug this document already records: Genesis Plus GX serves four
platforms, and a shader picked for Genesis was silently carrying into Sega CD,
Master System and Game Gear. That is the third independent confirmation of the
platform-keying rule, and it should settle it.

A stored value for a shader a platform no longer offers falls back to None
rather than being trusted.

**The history is worth keeping, because it is a warning.** The original six came
from RomM/EmulatorJS's own bundled set. Two ScaleHQ scalers and a `crt-geom`
slang port *"looked bad enough in this Metal port that MMagTech dropped them on
sight"*. A shader that is well regarded elsewhere is not automatically good once
reimplemented — judge each on the panel.

#### The letterbox glow

Separate from shaders, and **a television feature specifically**: tvOS and Mac
compile it, iOS does not, because *"the phone's screen has no dead space worth
lighting."*

It lights the dead area around the picture, ramping out from the game's edge to
the physical edge of the screen. Three settings, and the numbers are not round:

| | Peak white opacity at the picture's edge |
|---|---|
| Off | 0 |
| Subtle | **0.025** |
| Strong | **0.04** |

Reach is fixed at 100% — the ramp always travels the whole dead space. A
separate reach control was built and then dropped once 100% proved to be the
only value worth having.

**CabinetOS needs this more than Cabinet does, not less.** An integer-scaled
Game Boy picture on a 4K television is a small bright rectangle in a very large
black field — which is precisely the case the glow exists for, and it is the
normal case here rather than an edge one.

**And the tuning lesson is the one this project keeps relearning.** Two sets of
preset values guessed from a mockup were both wrong on real hardware — bleeding
into the picture, banding, not reaching the edge. A continuous slider on a real
panel found the numbers, and only then were presets chosen. Do not guess these;
build the slider.

### The in-game overlay, and the input-mode rule

The overlay is not composited by anything clever. **The frontend owns the frame
loop, so the pause menu is simply a view drawn over the game surface** — a
scrim, a panel, and buttons. That is the whole mechanism, and it is the direct
payoff of hosting cores in-process rather than launching them.

The one hard rule is about input, and it is architectural:

> **While a game runs, the controller belongs to the core exclusively. While the
> overlay is open, it belongs to the UI.** Never both.

On tvOS this is `controllerUserInteractionEnabled`, flipped by whether the menu
is visible. Without it the focus engine kept consuming presses, so B read as "go
back" and dismissed the player instead of reaching the core as a face button —
reported from real hardware as *"controllers work on the homescreen but in game
b exits the game"*.

CabinetOS has no focus engine handed to it, so it must implement both halves —
but it must implement the *rule*, not just the routing. Any design where a
button can mean two things at once is the same bug.

**And CabinetOS made it anyway, 2026-09-14.** The first game launched from the
library was played with the arrow keys, and those arrows moved the Tetris piece
*and* shifted focus on the Home screen behind it — so leaving the game would
have landed on something nobody chose. Same bug, different platform, found the
same way: by someone actually playing it rather than by reading the code.

The fix is one `InputOwner` asked once per event — Keyboard, Game, or UI — and
not a `!playing` check added at each call site, which is the shape this section
warns against. Worth noting *why* the bug survived a careful read: the core's
input is polled per frame from `SDL_GetKeyboardState`, while the UI's comes from
the event queue. Two different mechanisms, so nothing in either one looks wrong
on its own, and only the rule catches it.

---

## Controls: what CabinetOS inherits, and what it cannot

Raised 2026-09-13. The input *model* in *What CabinetOS is* does not change —
the controller is required, keyboard and mouse are supported and never needed.
What changes is the machinery underneath, and it is not a port of Cabinet's.

Checked on the running image rather than assumed.

### The rules that carry over unchanged

- **The controller belongs to the core, or to the UI, never both.** On tvOS this
  was forced: the focus engine kept consuming presses, so B read as "go back"
  and dismissed the player instead of reaching the core. On Linux nothing fights
  us — but the rule stands, because the same button still has to mean two things
  at two times, and any design where it means both at once is the same bug.
- **Mapping is ours, not the user's**, per *Emulation*.
- **The libretro device type is per PLATFORM, not per core** — a 3-button versus
  6-button Genesis pad is `retro_set_controller_port_device`, not a core option.
  See *Cores and platforms are not the same list*.

### What Linux gives us that tvOS could not

**Verified present in the image**, all of it from Bazzite and none of it ours to
maintain:

| | |
|---|---|
| `gcadapter_oc` | The official **GameCube adapter** — four real GC pads. Dolphin's native input, on the machine that runs Dolphin. |
| `hid-playstation`, `hid-nintendo`, `hid-sony`, `hid-steam` | DualSense, DualShock, Switch Pro, Steam Controller, in-kernel |
| `xone_*`, `xpad` | Xbox wired and wireless, including the dongle |
| `hid-fanatec`, `hid-t150`, `hid-tmff-new`, `hid-logitech-new` | Four force-feedback **wheel** drivers |
| `psxpad-spi` | Real PlayStation pads over SPI |

And the one that matters most for a cabinet: **real spinners, trackballs, dials
and light guns are just input devices here.** MAME 2003-Plus is in the set
specifically for the early-80s boards whose controls were exactly those, and
Cabinet can only offer them as a touchscreen approximation. CabinetOS can take
the real thing. That is a capability the reference implementation does not have
and cannot get.

Also: **Steam is gone, so Steam Input is not in the way.** Pads arrive raw and
the mapping is entirely ours.

### What is harder here, and none of it is optional

1. **One controller can appear as several devices.** A DualSense over Bluetooth
   presents its gamepad, its motion sensors and its touchpad as separate evdev
   nodes. Enumerate naively and a console with one pad says three are connected.
   SDL's gamepad layer collapses most of this; it does not collapse all of it,
   and the settings screen must show what a person recognises as *their
   controller*, not a device list.
2. **Unknown controllers exist.** tvOS took a curated list. Linux takes anything
   that speaks HID. SDL3 carries a large built-in mapping database, but a pad it
   has never seen produces a working device with meaningless buttons. **Cabinet
   never needed a remapping screen; CabinetOS does** — and it has to be usable
   with the very controller whose buttons are wrong, which means driving it by
   position ("press the button below the others") rather than by name.
3. **Player assignment is ours.** Apple hands out `playerIndex`. Here, which pad
   is player one is a decision, it has to be visible, and it has to survive a
   controller sleeping and reconnecting mid-session. Four-player arcade and
   GameCube make this real rather than theoretical.
4. **Bluetooth pairing is ours to build.** tvOS had Apple's own Settings; bluez
   over D-Bus is the equivalent and there is no UI for it in the image. It has a
   chicken-and-egg at the centre: **the first controller cannot be paired using
   a controller.** The answer is that a pad connected over USB works
   immediately, and pairing is reachable from there — which must be *said* in
   first-run setup rather than left to be discovered.
5. **Rumble quality varies by driver**, and there is no Taptic Engine to fall
   back to the way Cabinet has on a phone.

### The permission detail, and a Phase 2 decision that paid for itself

Gamepads are the one input the frontend reads **directly from `/dev/input`**;
keyboard and mouse arrive through Wayland from the compositor. Different path,
different failure mode, and it is worth knowing which is which when something
does not respond.

`/dev/input/event*` is `root:input` mode `0660`, and the session user is **not**
in the `input` group. Access comes from an ACL instead:

```
SUBSYSTEM=="input", ENV{ID_INPUT_JOYSTICK}=="?*", TAG+="uaccess"
```

systemd-logind applies that ACL **to the active session on the seat**. The
CabinetOS session is `Seat=seat0`, `Active=yes` — verified — because Phase 2
gave it `PAMName=login` and a real logind session rather than running it as a
bare service.

**So a Phase 2 decision is what makes controllers work at all in Phase 5.** Had
the session been a plain unit with no PAM session, every gamepad would be
unreadable and it would present as a controller bug rather than a session bug.
Recorded here so that nobody "simplifies" the unit later and spends a week on it.

*Not yet verified:* no gamepad has been attached to the VM. The rule and the
seat are confirmed; the ACL actually appearing on a real pad is not.

---

## Developer mode

CabinetOS has no terminal, no file browser and no package manager. That makes it
a console, and it also makes it very hard to work on — and hard for anyone else
to contribute to.

**Developer mode is the single sanctioned exception.** It is hidden, off by
default, and explicitly opted into from Settings. When enabled it provides:

- **SSH** — shell access to the machine.
- **SFTP** — file transfer, so a new frontend build can be pushed to a running
  console without reflashing it. This is the thing that makes the Phase 3 to
  Phase 5 development loop bearable.

Rules it must follow:

1. **Hidden by default.** A normal user browsing Settings must not stumble into
   it. It is not a visible toggle with a scary label; it is somewhere you have
   to know to look.
2. **Off until deliberately enabled**, and the state survives reboots and system
   updates.
3. **Visibly on when it is on.** If SSH is listening, the UI says so somewhere
   the user will see it. A console that is quietly accepting remote logins is
   not acceptable, even on a home LAN.
4. **Enabling and disabling it is controller-driven**, like everything else. The
   person turning it on does not yet have a shell.
5. **It does not change the rest of the product.** No terminal appears in the
   UI, no desktop becomes reachable, nothing about the console experience
   changes. It opens a door from outside; it does not put one inside.
6. **Turning it off actually stops the service**, rather than only hiding the
   toggle.

The shipping, user-facing version of this lands in **Phase 6**. A cruder
build-time escape hatch will be wanted earlier — see open question 8.

---

## Staying current with Bazzite

The base is pinned by digest, so CabinetOS never changes underneath itself. The
cost of that is somebody has to move the pin, and a pin nobody moves is how a
project ends up two years behind its base with an unreviewable upgrade ahead of
it.

`.github/workflows/base-update.yml` runs weekly and opens a pull request when the
pinned digest has moved.

> **It needs a repository setting to do the second half, and it did not have
> it.** *Settings → Actions → General → "Allow GitHub Actions to create and
> approve pull requests"* is off by default, and with it off `gh pr create`
> fails with `GitHub Actions is not permitted to create or approve pull
> requests`. The branch is pushed before that line runs, so the failure leaves
> a branch and no pull request.
>
> That is what happened on its **first and only run**, 2026-09-14: the base
> moved, the branch appeared, the run went red, and the bump was found three
> days later by someone tidying up branches. **A half-finished automation is
> harder to notice than one that never ran** — a workflow that fails completely
> is a red mark on a page somebody looks at, while this one produced a
> plausible-looking branch and a failure nobody was watching for.
>
> The workflow now says so in its own job summary when the call fails, with the
> setting named and the command to open the pull request by hand.
>
> **The setting was turned on 2026-09-17 and the workflow opened its first real
> pull request the same day.** Two more things about running it, both found
> that day:
>
> **A run on the bot's pull request lands as `action_required` and waits.**
> GitHub gates workflow runs on pull requests it treats as untrusted, and
> `github-actions[bot]` is one of those. So the build does not start until
> somebody approves it — the button, or:
>
> ```
> gh api -X POST /repos/MMagTech/cabinetos/actions/runs/<run-id>/approve
> ```
>
> A ROUTINE bump is supposed to be mergeable on the strength of a green build,
> and there is no green build until this happens.
>
> **Do not delete the branch while its build is running.** Closing the first
> attempt and deleting its branch killed the in-flight image build, which then
> showed as a 16-minute `failure` with zero jobs and no logs — and reads
> exactly like the new base failing to build, which it was not.

#### What the first real run got wrong, and what it cost to find out

**Both directions at once**, on 44.20260914 → 44.20260916:

- It reported **one** relevant change, a Fanatec steering wheel driver, while
  `amd-gpu-firmware`, `amd-ucode-firmware` and `linux-firmware` itself went
  **backwards by a month** — 20260910 to 20260810 — filed under the full diff
  beside fonts and translations. `base-watch.txt` had no firmware entry at all:
  kernel modules were covered by `kmod-*`, and firmware matched nothing. The
  reference SER5 is AMD, and its GPU firmware and CPU microcode are not
  incidental to this project; they are the machine.
- **156 of its 193 reported changes were `gpg-pubkey`** — 81%. Not a package:
  an entry in RPM's keyring, and a dozen of them share one NAME with different
  versions, so joining the manifests on NAME turned thirteen entries into a
  hundred and sixty-nine pairs.

Fixed the same day. The regenerated pull request reported 22 relevant changes
led by the firmware, and zero keyring lines.

> **The lesson is about the watchlist, not about firmware.** The file's own
> header says to add to it as the project grows, and warns that a package
> CabinetOS relies on but does not list "can break silently in a base bump that
> looked routine". That is precisely what happened, on the very first run, in a
> category nobody had thought of. **When something goes wrong on real hardware,
> check what this file does not watch** before assuming the base is innocent.

**The 44.20260916 firmware move is Bazzite's, and it is a fix rather than a
regression.** Chased down 2026-09-17 rather than left as "upstream did it":

1. Fedora shipped `linux-firmware` **20260910** to F44 stable on 10 September —
   confirmed against Bodhi, where it is still listed stable, so Fedora did not
   pull it.
2. It caused problems on some handhelds.
3. `ublue-os/bazzite`, **15 September**: *"fix: Allow firmware pinning, pin to
   old version of firmware due to issues with the latest on some handhelds"*.
4. Their 16 September stable image carries that pin, and that is the image this
   project now builds on.

> **Taking the stable channel means taking Bazzite's judgement about firmware,
> and that is the point of tracking a base.** They have hardware reports across
> a fleet of devices; this project has none, and no hardware at all until the
> SER5 is installed. The SER5 is a mini PC rather than a handheld, so it is
> probably unaffected either way — and right now the older firmware is the
> better-tested one.

**The flag was still correct, and this is the useful shape of the lesson.** A
month-long firmware move should always surface for a read. It simply turned out
that reading it said "good, they caught something" rather than "be careful" —
which is what a working watchlist looks like most of the time, and is not a
reason to narrow it.

**The tag written into `Containerfile` is the image's version label, not the
channel tag** — `44.20260916` rather than `stable-44.20260916`. Checked against
the registry: `stable`, `44.20260916` and `stable-44.20260916` all resolve to
the same digest, so nothing is ambiguous and the `FROM` line would still
resolve if the digest were ever dropped. Worth knowing before somebody reads the
missing `stable-` prefix as a channel change, which is what it looks like in
`git log`.

**Why not Renovate or Dependabot.** Bazzite rebuilds daily. A dependency bot
would open a pull request every day that said "digest changed" and nothing more.
A pull request that arrives every day and carries no information is worse than
no automation at all, because it trains you to merge without reading.

So `ci/check-base-update.sh` does the part a bot cannot:

1. **Diffs the package manifests.** `base-manifest.txt` is the base's package
   list as of the last bump, committed alongside the pin. The PR shows what
   actually changed — added, removed, and version bumps — instead of a digest.
2. **Classifies it.** Changes are matched against `ci/base-watch.txt`, the list
   of packages CabinetOS actually depends on: kernel, Mesa, gamescope,
   controller kmods, bluez, PipeWire, tuned, NetworkManager, sshd. The PR is
   labelled **RELEVANT** (read it) or **ROUTINE** (a green build is probably
   enough). Most weeks are routine.
3. **Flags strip-list drift.** A package the strip scripts remove which existed
   in the old base and is gone from the new one. `remove_pkgs` skips missing
   packages by design, so nothing breaks — but if upstream *renamed* it rather
   than dropping it, the real package is still in the image and the removal has
   silently become a no-op. That is how a desktop application comes back.

**`ci/base-watch.txt` must grow with the project.** Phase 2 adds whatever the
session depends on; Phase 5 adds the emulators. A package CabinetOS relies on
that is not on that list can break in an update that looked routine.

**Nothing is merged automatically, ever.** An image that builds is not an image
that boots. Build a qcow2 from the branch and boot it first — constraint 2 above
is the rule this automation serves, not one it replaces.

**Known limitation:** pull requests created with the default `GITHUB_TOKEN` do
not trigger other workflows, so the image build will not run on them by itself.
Either supply a `BASE_UPDATE_TOKEN` secret (a fine-grained PAT with contents and
pull-request write), which the workflow prefers when present, or close and
reopen the PR to kick CI off by hand.

---

## Measured behaviour

Numbers from the first booted image, a 1GB Unraid VM, 2026-09-13. Replace these
when they are measured again on real hardware — a VM has no GPU, so nothing here
says anything about graphics or emulation.

### Resource use is about services, not packages

**Installed packages that never run cost nothing.** Plasma is still installed and
contributes zero: the default target is `multi-user.target`, both display
managers are masked, and there are no Plasma processes. Removing another
thousand packages would shrink the image and free no memory at all.

What costs memory is **services that run**. At idle: **629 MB used, 32 running
services, 18.7s boot** (14s of it userspace).

Stopping seven services that a console has no use for recovered **91 MB, 14% of
idle memory**, and took the service count to 24:

| Service | Why it has no place here |
|---|---|
| `input-remapper` | 84 MB across two processes, the single largest consumer. CabinetOS owns controller mapping itself (Phase 5). |
| `cardwired` | 38 MB, and **6.8s of the 14s boot** — the slowest unit on the system. |
| `ModemManager` | Cellular modems. |
| `displaylink` | USB display adapters. |
| `gssproxy` | Kerberos/NFS credentials. |
| `systemd-homed` | Portable home directories. |
| `upower` | Battery monitoring on a mains-powered console. |

Deliberately left alone, and why: `tuned` (41 MB) is the power and thermal
management the project depends on; `firewalld` (50 MB) is a security posture
decision, not a performance one; `uresourced` and `dmemcg-booster` are Bazzite's
game process-priority layer; `ds-inhibit` stops controllers being treated as
keyboards for idle purposes.

**So the Phase 8 performance lever is the service list, not the package list.**
Worth ~90 MB and ~7s of boot before touching anything contentious. Emulation
performance itself will be bound by GPU throughput and single-thread CPU speed,
neither of which any of this affects.

### The A9 Max, measured — 2026-09-19

**The reference machine is installed and running CabinetOS, on its own GPU.**
Everything below is read off the machine rather than hoped for. This is the
first time any of it has been true.

```
amdgpu 0000:c6:00.0: VRAM: 4096M ... 4096M of VRAM memory ready
amdgpu 0000:c6:00.0: SMU is initialized successfully!
amdgpu 0000:c6:00.0: [drm] Display Core v3.2.384 initialized on DCN 3.5
amdgpu 0000:c6:00.0: [drm] DMUB hardware initialized: version=0x09004E00
[drm] Initialized amdgpu 3.64.0 for 0000:c6:00.0 on minor 1

cabinetos-session: trying gamescope (drm)
cabinetos-session: gamescope (drm) is up
[gamescope] version 3.16.28-ogc3+

[cores]   /usr/lib/cabinetos/cores
[storage] root /var/lib/cabinetos
[storage] linked PPSSPP into the system directory
[storage] user 1 - MMagTech
[frontend] GL_RENDERER AMD Radeon 890M Graphics (radeonsi, strix1, ACO, DRM 3.64)
[library] 1147 playable games, 1147 with art; 501 games skipped
```

| | |
|---|---|
| Compositor rung | **gamescope on drm** — the top one. The VM has only ever reached cage. |
| Session restarts | **0** |
| Renderer | **radeonsi / strix1 / ACO**, not llvmpipe |
| Vulkan | **AMD Radeon 890M Graphics (RADV STRIX1)** — open question 20's prerequisite, present |
| Address | `cabinet@192.168.1.212`, key installed; sudo password `cabinet`, the same throwaway as the VM |

**What this unblocks.** Vulkan existing on this machine is what open question
20 was waiting for, and it serves three systems at once — RPCS3, parallel-RDP
and Flycast. PS3 could not be *played* anywhere before today.

#### The output was hardcoded to 1080p on a 4K panel

**The one thing wrong on first boot, and it matters more than it sounds.**
`system_files/usr/bin/cabinetos-session` passed `--output-width 1920
--output-height 1080` as literals, written in Phase 2 before anything had ever
been plugged into it. The display reports `3840x2160`, gamescope obeyed the
literals, and the panel scaled the result.

That is the worst possible state for the work that follows: the frontend's
shapes are signed-distance fields and render exactly at any resolution —
verified at 3840x2160, 1920x1080 and 1280x720 — so **every soft edge on that
screen was the television's scaler and none of it was the design.** Judging
the look against it is judging the wrong picture, the same error as tuning
motion on llvmpipe.

**Removing the flags is NOT the fix, and that was measured rather than
assumed.** With no `--output-width`/`--output-height` at all, gamescope still
chose `1920x1080@60Hz` on a display offering 3840x2160. The mode has to be
found and passed. The session now reads the first line of a connected
connector's `modes` file — the kernel lists them preferred-first — and
`CABINETOS_OUTPUT=WxH` overrides it.

**Cabinet is the reason this is not a trade-off.** Cabinet chooses no
resolution at all: there is no `nativeBounds`, no `preferredDisplayMode` and
no 3840 anywhere in the app, because tvOS hands it a canvas. Its own docs
reason against a 4K output — Game & Watch is iOS-only partly because a
562x374 canvas "goes soft on a 4K television, roughly a 7x blowup". So a 4K
presentation is what the reference implementation gets, and core parity with
Cabinet is a hard constraint here.

### Session infrastructure present

`gamescope` is in the image and runs. `gamescope-session-plus` is not — see open
question 3. The only Wayland session defined is `plasma.desktop`, which Phase 2
removes.

### What the image actually contains — 2026-09-19

Measured while putting the console into it, on real files rather than on
estimates. Phase 5, *The deploy*, has the reasoning; these are the numbers.

| | |
|---|---|
| The frontend binary | **868 KB** |
| Twenty-one cores | **259 MB** |
| PPSSPP's system files | **13 MB**, 12 entries |
| **Added to the image** | **273 MB** |

**Every library resolves, and that was run rather than reasoned about.**
`ldd` over the binary and all twenty-one cores, inside the Fedora 44 builder
container and again on the test VM — which *is* CabinetOS, so it is the image's
own library set — reports nothing unresolved in either place.

**The control was run too, and it is the more informative half.** The same
install into a bare `registry.fedoraproject.org/fedora:44`, which has none of
the graphics stack, names all six libraries the frontend is missing —
`libSDL3`, `libEGL`, `libGLESv2`, `libfreetype`, `libjpeg`, `libpng16` — plus
`libGL` for melonDS and **`libX11` and `libXext` for PPSSPP**, which nothing
had ever written down. `ldd` prints `=> not found` and still exits 0, so the
check reads its output and not its status.

**`mesa-libGLES` is not in the base image and libGLESv2 is there anyway**, via
`libglvnd-gles`. Worth knowing before somebody "fixes" a package list: the
frontend and three cores link `libGLESv2.so.2`, and looking for the obvious
package name finds nothing.

### A GPU-less VM *can* show the frontend, via cage

Established 2026-09-13, correcting an earlier claim in this document that
nothing visual could be developed without real hardware.

**gamescope requires a Vulkan device it accepts**, and a VM's virtual GPU does
not provide one. The image ships the Venus driver (`virtio_icd`), but the host
must expose Vulkan over VirtIO-GPU for it to do anything, and Unraid's QEMU does
not — the guest reports `+virgl` but Vulkan enumeration finds no devices at all.
So gamescope falls to Mesa's software renderer and rejects it outright:
`vulkan: selecting physical device 'llvmpipe' ... not a valid physical device`.

**But `cage` is already in the image and renders in software.** Confirmed
running under the real session service, with a test application visible on the
VM's console. cage is a minimal kiosk compositor — one fullscreen app, the same
basic job as gamescope, without the display features.

The session script therefore works down a ladder: gamescope on drm, then cage,
then gamescope headless. **The frontend does not care which is hosting it** — it
is a Wayland client either way.

What this changes: **Phase 3 can be built and looked at in a VM.** Only
performance, display features and final integration need the SER5.

What it does not change: cage has no VRR, no HDR and no scaling, so it is a
development convenience, never the production path. Falling back to it on real
hardware means something is wrong with the GPU, and the About screen should name
the running compositor so that state is visible rather than mysterious.

### First boot shows Linux

**And as of 2026-09-19 the boot after it shows the frontend**, which it did not
before: the session ran `sleep infinity`, so an installed machine showed Linux
and then showed nothing.

`bazzite-hardware-setup.service` runs visibly on first boot and takes long
enough to notice. `plasma-setup.service` — Plasma's out-of-box wizard — is
present and inactive only because nothing starts a graphical session.

Both are "the user sees Linux" moments, which the product rules out. Phase 2
should remove `plasma-setup` outright and either own the hardware-setup step or
hide it behind a splash.

`plasma-setup.service` runs a `bootutil` binary that decides whether to show the
wizard based on a `plasma-setup-done` flag file. Dropping the flag file in place
would suppress it, but removing the unit is cleaner — CabinetOS owns first run.

### The cache is not the only thing a game writes to disk

**Measured 2026-09-16, by running Flycast and Mupen64Plus and then looking at
what appeared**, rather than by reasoning about what a core ought to write.
Three kinds of file, none of them a ROM, and **eviction sees none of them**:

| What | Where | Size after two games |
|---|---|---|
| Mesa's compiled-shader cache | `~/.cache/mesa_shader_cache/` | 2.5 MB |
| Mupen64Plus's driver database | `bios/Mupen64plus/mupen64plus.ini` | 447 KB |
| Flycast's Dreamcast flash | `bios/dc/dc_nvmem.bin` | 131 KB |

**The paths changed on 2026-09-18 and the problem did not.** `cache::candidates`
walks `cache/` and nothing else — that is the whole of the rule now, and it is
checkable by listing a directory rather than by reading code. But it still means
everything above consumes free space, is counted by the floors as simply gone,
and **cannot be reclaimed by any code this console has**. Un-keeping every game
would not shrink it by a byte.

It is small today and the shapes differ, which is why they are listed
separately rather than as one number:

- **The shader cache is the one that grows with play.** It is the graphics
  driver's, not the core's, and it is written for every shader a hardware
  core compiles — so it grows with how many different games have been played
  and resets whenever Mesa is updated. Mesa evicts it against a **default cap
  this console inherits rather than sets**, which is the wrong way round for an
  appliance: the size should be a number CabinetOS chooses, via
  `MESA_SHADER_CACHE_MAX_SIZE`, and the Storage screen should be able to say
  what it is.
- **The system directory is mixed**, and that is the part to be careful with.
  `mupen64plus.ini` is a database that can be deleted and will come back.
  `dc_nvmem.bin` is a Dreamcast's saved flash — **console settings, and a
  machine fact rather than a person's.** Treating the system directory as
  reclaimable would throw it away, and it is the cheapest of the three to lose:
  it rebuilds itself. Anything that cleans here has to distinguish the two,
  which is the same distinction the Storage screen already draws between a
  cache and a kept game.

  **The VMU that used to live beside it is gone from here, as of 2026-09-19.**
  Open question 18 named `bios/` holding one irreplaceable file as the single
  place its own shape did not answer the question, and the Dreamcast save work
  took that file out — the card is placed for the length of a session and moved
  into the person's save tree at the quit. What is left in the system directory
  is genuinely the machine's own emulator state.

Nothing here is urgent — it is under 3 MB against a 5 GB system reserve — but it
is a category the storage model currently does not have, and it arrived with the
hardware-rendered cores rather than existing before them.

**PS3 makes this category much bigger, measured 2026-09-18.** RPCS3 keeps a PPU
recompiler cache of its own, outside the virtual hard drive entirely — under the
emulator's config directory, so on the OS volume rather than the games one. It
reached **21 MB while failing to reach a title screen**, because it caches
compiled code for every module the game loads and a PS3 game loads dozens. This
is the first entry in this table that could plausibly run to gigabytes, and the
first where the *location* is wrong as well as the size: a games drive should
hold it.

### Every platform, run once — 2026-09-19

**Twenty-six platform rows, one game each, and until this was run nobody could
say which of them worked.** The claim in the handover was *1147 games playable,
all twenty-one cores can be RUN*. That was true when it was measured, and then
the on-disk layout moved and two platforms broke without anybody noticing. So
the claim was not something to stand behind.

The test is deliberately not a look-and-feel judgement, so it does not wait on
the reference machine: launch the SMALLEST game on each platform headless, and
ask three things.

1. **Does the core load it?**
2. **Does it reach a running state?**
3. **Does it draw anything that is not black?**

The third is there because of what PPSSPP taught: a capture can be a plausible,
nearly-black picture with legible text and a maximum pixel of **(4,4,4)**, and
it reads as "this core renders black" until somebody reads the number. So the
harness reports the maximum and mean pixel of every frame it takes.

#### The result, after the three faults below were fixed

| | | | |
|---|---|---|---|
| Atari 2600 | ran, max 231 | Neo Geo Pocket | ran, max 255 |
| Atari 7800 | ran, max 255 | Arcade — FBNeo | ran, max 255 |
| Vectrex | ran, max 248 | Arcade — MAME | ran, max 255 |
| NES | ran, max 255 | Nintendo DS | ran, max 255 |
| Game Boy | ran, max 255 | Nintendo 64 | ran, max 255 |
| Game Boy Color | ran, max 247 | PlayStation | ran, max 255 |
| Game Boy Advance | ran, max 255 | **Saturn** | **ran, max 248** |
| Master System | ran, max 255 | Sega CD | ran, max 239 |
| Game Gear | ran, max 255 | TurboGrafx-CD | ran, max 255 |
| Genesis | ran, max 239 | 3DO | ran, max 223 |
| Sega 32X | ran, max 239 | Dreamcast | ran, max 255 |
| SNES | ran, max 255 | PSP | ran, max 255 |
| TurboGrafx-16 | ran, max 255 | | |

**Every platform this console claims to play, plays.** It is the first time
that sentence has been measured rather than asserted.

#### Two false alarms, and they cost an hour each time they are rediscovered

**A dark first capture is usually a slow boot, not a broken core.** Neo Geo
Pocket and PlayStation both came back at max=8 — indistinguishable from black —
on the first pass, and both are simply slow: **PlayStation needs about 6000
frames to get past the Sony logo on this VM**, Neo Geo Pocket about 1200, Sega
32X about 2000, Saturn about 2600. The discriminating test is to capture at two
frame counts and see whether the picture moves.

**And a Saturn capture is nondeterministic.** Death Crimson gave max=209 at
2000 frames on one run and max=8 at 2000 frames on the next, then max=248 twice
at 2600. The CD emulation is not frame-deterministic under this harness, so a
single dark capture of a CD platform means nothing on its own.

#### Fault one: a core looks its BIOS up by a name RomM does not use

**No Saturn game could start.** Not one, and nothing said so until a game was
launched:

```
[firmware] saturn_bios.bin (524288 bytes)
[core] Cannot open BIOS file ".../bios/sega_101.bin".
[launch] the core refused .../Death Crimson (Japan).chd
```

The file downloaded correctly. The core looked for a different name. **Both are
right and nothing joined them up** — RomM serves firmware under whatever name
the person who uploaded it chose, and every core hardcodes the names it will
try and gives up if none are present.

It is the third instance of this shape in one day. 3DO was the same and only
works because `opera_bios` is answered with `panafz10.bin` and the reference
server happens to use that name. Sega CD and TurboGrafx-CD happen to match.
**Two of the four firmware platforms were working by luck.**

`catalog::firmwareAliases` closes it, and the reasoning is the reference
implementation's:

- **Match on SIZE**, because RomM's firmware record carries a filename and a
  length and nothing else. There is no region field and no purpose field.
- **Copy under EVERY name the core might try**, not the likeliest one. Beetle
  Saturn and Genesis Plus GX pick their CD BIOS from the DISC's region code at
  load time with no fallback, so which name is needed is not knowable when the
  file is being placed.

**AND ONLY FROM THIS PLATFORM'S OWN FIRMWARE LIST.** The first version scanned
`bios/` by size, which reaches across platforms — and very nearly did: the
PlayStation BIOS is **524288 bytes, exactly Saturn's size**, and the two were
separated only by alphabetical order. `saturn_bios.bin` sorts before
`scph1001.bin`. It picked correctly and that is not a property to rely on.

#### Fault two: Dreamcast was running a fake BIOS and saying nothing

Flycast reads the boot ROM from `dc/` inside the system directory. This console
put it at `bios/dc_boot.bin`, one level up, and **`bios/dc/` never contained it**
— checked five times over the course of the day. When Flycast cannot find the
file it falls back to **reios**, its own built-in approximation, with no error
at any log level.

So the console has been emulating a Dreamcast with a substitute boot ROM for as
long as Dreamcast has worked. Games run, which is exactly why nobody noticed.

**Confirmed by the picture, which is the only place it shows.** With the boot
ROM staged into `bios/dc/`, frame 400 of Ikaruga is the Dreamcast startup
animation — the wordmark with the orange swirl being drawn. reios has no logo
and boots straight into the game. The difference is one frame capture and it
was invisible everywhere else.

#### Fault three: a web page was accepted as a game and played

**Three entries in the reference library are not games.** Gangster Town, Rambo
III and Assault City on Master System are 5 to 9 KB files beginning
`<!DOCTYPE HTML>` — error pages from wherever the ROMs were fetched, saved with
a `.7z` extension. RomM serves them with **HTTP 200**, content type
`application/octet-stream`, and `missing_from_fs: false`, so nothing on the
server side flags them either.

**What this console did with one:** downloaded it, handed it to Genesis Plus
GX, which accepted it, reported correct Master System geometry — 256x192 at
59.92 Hz — ran, and drew black. Three thousand frames later, still black. No
error anywhere in the log.

`romfile::sniff` already identifies a payload by its leading bytes to decide
whether to unpack it. It now also recognises the one case where the bytes say
outright that this is not a game, and the launch refuses with a sentence:

```
[launch] ... is a web page, not a game; deleted rather than kept, because a
         cached one would fail the same way for ever
```

**The delete is the load-bearing half.** The download path skips a file already
on disk at the expected size, so a cached error page would match for ever and
that game would be permanently broken with no way back from inside the product.
A download that is not a game is not a download.

Matched only on `<!doctype` and `<html`, case-insensitively, after any byte
order mark and leading whitespace, and checked LAST so nothing that is a real
container can fall into it. A ROM beginning with those bytes is not a file
anyone has.

### Other facts worth keeping

- The installed system is **7.9 GB**. `/` is a 43 MB read-only composefs; all
  real storage is `/var`.
- `sshd.service` is enabled, `sshd.socket` disabled — the classic always-listening
  form, not socket activation.
- The hostname is `bazzite`. Branding, Phase 8.
- `systemd-udev-settle.service` costs 4.3s at boot and is deprecated upstream.
  Worth investigating what still pulls it in.

---

## Branding and the boot experience

### The palette

Taken from Cabinet's own icon generator, `tools/make_icon.swift`.

**Verified against the source 2026-09-13: every value below is correct.** One
detail was missing and is added — the backdrop's middle stop sits at 0.55, not
at the midpoint.

| Role | Value |
|---|---|
| Backdrop | `#3A2268` (0.0) → `#120C26` (**0.55**) → `#090614` (1.0), vertical, top to bottom |
| Cabinet body | `#EEEAE2` |
| Marquee | `#FF7AC7` → `#FFC457`, horizontal |
| Screen | `#58E8F6` → `#2484D6`, vertical, with a white sheen at 26% fading out |
| Control panel / base | `#CEC7BC` |
| Joystick | `#3A3444` |
| Buttons | `#EC405C`, `#FFC457` |

**But this is the icon's palette, and the app does not use it.** That is worth
saying plainly, because this document previously implied otherwise.

Cabinet has **no colour assets and no design tokens at all** — checked: the
asset catalogues hold app icons and nothing else, and there is not one
`.colorset` in the repository. What the app actually looks like comes from three
places, none of which is the icon:

1. **Black, white and system semantic colours.** `Color.black` (57 uses),
   `Color.white` (41), `.primary`, `.secondary`, `.tertiary`. Everything is
   dark; `colorScheme` is forced to `.dark` where the platform would otherwise
   have a say.
2. **System materials** — `.regularMaterial`, `.ultraThinMaterial`, and on
   tvOS 26 real Liquid Glass. This is the single largest contributor to the
   look, and it is the thing CabinetOS gets none of for free.
3. **The artwork itself.** Cover art is the brightest thing on every screen, by
   explicit design, and backgrounds are almost always a blurred, desaturated,
   darkened copy of the art in front of them.

There are exactly **two** deliberate colours in the whole app:

| | Value | Where |
|---|---|---|
| Platform tile panel | `#241A3D` | `TVLibraryView.panel`, the library's tile background |
| Accent | **unset** — Apple's default | the prominent pause-menu button, progress tints |

The panel colour carries a note worth keeping: it is *"deliberately darker and
less saturated than it looks in a browser mockup: the same sRGB values render
far more vividly on a wide-gamut TV, and the first build of this tile came out a
loud electric purple on real hardware."* CabinetOS ships to televisions. Tune on
one.

**The accent colour is a decision CabinetOS has to make and Cabinet never did.**
Nothing sets one, so `Color.accentColor` is whatever Apple's default tint is on
the platform. A Linux frontend has no such default. The icon offers the obvious
candidates: the screen cyan `#58E8F6` reads as "powered on" and has the contrast
for a focus tint against a dark ground; the marquee pair `#FF7AC7`/`#FFC457` is
warmer and more arcade. Pick one in Phase 3 and put it in the token table below.

### The boot splash

**The icon is an arcade cabinet, so the splash is an arcade cabinet powering
on.** It appears the moment the firmware hands over, dark; the marquee lights,
then the screen glows; it holds until the frontend has drawn its first frame.

It covers the three things that otherwise show Linux to the user: the scrolling
kernel text, Bazzite's first-boot hardware setup job, and the gap before the
frontend is ready.

The handoff from splash to frontend must have no black flash in it. That is the
fiddly part, and it is what separates a console from a Linux box with a nice
wallpaper.

**Text on the splash: the wordmark "CabinetOS" and nothing else.** A boot screen
names the machine; it does not explain it.

**Development builds may show a version string**, small, in a corner — genuinely
useful when a VM and a mini PC are both running different builds. Off for
release builds.

### Attribution belongs in Settings → About, not on the boot screen

Considered and rejected: putting "a fork of Bazzite" on the splash. It breaks
the product's own rule — that line tells the user they are looking at a Linux
distribution, which is exactly what the rest of the design works to avoid. No
console does it: a PS5 does not say "built on FreeBSD", an Apple TV does not say
"based on Darwin". And the audience is wrong, because the people who care are
reading this repository, not squinting at a television.

Credit instead goes where Sony and Apple put it: **Settings → About**, with full
acknowledgement of Bazzite, Universal Blue, ChimeraOS, and the emulator projects
whose work this is built on. Also in the README, where it already is.

This is not a licensing question — Bazzite's licence is satisfied by crediting in
the documentation. It is a question of what the product should feel like.

### What the boot chain actually looks like

| Stage | Ours? |
|---|---|
| Firmware logo | No. Vendor's, in the motherboard's own chip. Usually *disableable* in firmware settings, which is worth doing — black is cleaner than someone else's logo. |
| Boot menu | Ours. Hidden. |
| Kernel text | Ours. Hidden — needs `quiet` and `loglevel=0`, neither of which is set today. |
| Splash | **Ours.** |
| Frontend | Ours. |

Any PC-based console has the firmware seam; SteamOS and Batocera included. Real
consoles avoid it only by making the firmware too.

---

## The design system

The Phase 0 deliverable. Extracted from Cabinet's source 2026-09-13, written so
that someone who has never read Swift can reproduce it.

**Read the caveat first.** Cabinet is a SwiftUI app on a platform that hands it a
focus engine, a type ramp, a materials system and a navigation container. A large
part of "what Cabinet looks like" is tvOS behaviour that Cabinet never wrote
down, because it never had to. This section writes it down. Where a value comes
from Cabinet's own source it is stated as such; where it comes from the platform
it says so, and those are the places CabinetOS has to *build* something rather
than *match* something.

### The canvas

tvOS lays out in a **1920×1080 point space regardless of panel resolution** — a
4K television renders the same layout at 2×. Every number in this section is in
those points.

CabinetOS should adopt the same convention: **design at 1920×1080 and scale**.
It makes every number here directly usable, it matches what the reference
implementation was tuned against, and it means a 4K panel is a rendering
decision rather than a layout one.

**VERIFIED on the VM, 2026-09-13.** The frontend renders the same frame at
3840×2160, 1920×1080 and 1280×720 and the layout is identical to within a pixel
in design points — a focused cover measures 277.0, 278.0 and 277.5 design points
of visible fill against a predicted 277.2. It is rendered *natively* at each
size rather than upscaled: the UI's shapes are signed-distance fields, so a 4pt
rim is exactly 4pt and an edge is exact at any resolution.

**Most televisions this lands on will be 4K, and plenty will not be, and neither
may be assumed.** So the frontend can render offscreen at any size and read the
frame back (`--render-size`), which is how a 1280×800 development VM proves its
layout on a 4K set nobody here owns. Do that for any layout change; it costs
seconds.

Two consequences worth holding on to:

- **Non-16:9 panels letterbox rather than stretch.** Verified: the VM's own
  1280×800 output produces correct bars. A console puts the slack in bars; it
  does not distort the picture.
- **4K costs real fill rate.** Drawing rectangles at 3840×2160 is free, but
  Phase 8 should decide deliberately whether the *game* is upscaled by gamescope
  from its native resolution or rendered larger. That is a performance decision
  on Vega integrated graphics, not a layout one, and this canvas keeps the two
  separable.

**Overscan is real and the simulator lies about it.** Home's hero was sized
three times before it fit: 0.42/460 cut the shelf caption off, 0.34/380 still cut
it off *on real hardware although the simulator showed it fitting*, 0.28/300 fit
with room to spare, and it finally settled at 0.40/420. Budget a safe area and
verify it on a television, not on a screenshot.

### Colour tokens

Cabinet has none, so these are named here for CabinetOS to implement.

Two kinds of value below, and the difference matters. **Literal** values are read
straight out of Cabinet's source and are exact. **Semantic** values are what
Cabinet asks the platform for — `.secondary`, `.tertiary`, `Color.red` — so the
number given is Apple's documented dark-mode value, not something measured here.
Treat the semantic ones as the intended relationship and settle the exact numbers
when there is something on a television.

| Token | Value | Kind | What it is |
|---|---|---|---|
| `bg` | `#000000` | literal | The ground. Genuinely black, not near-black. |
| `bg-modal` | `#212121` → `#141414` vertical | literal | Full-screen covers with no artwork to blur (account, PIN, setup) |
| `surface` | `#241A3D` | literal | The one solid panel colour — library tiles. Source is `rgb(0.14, 0.10, 0.24)`; use those floats rather than the hex if there is any doubt. |
| `text-primary` | `#FFFFFF` | literal | |
| `text-secondary` | white @ ~60% | semantic | Platform labels, counts, captions under a title |
| `text-tertiary` | white @ ~30% | semantic | Chevrons, disclosure marks |
| `scrim-overlay` | black @ 55% | literal | Behind the pause menu, and over blurred backdrops |
| `focus-rim` | white @ 85% | literal | The 4pt ring on focused artwork |
| `accent` | **to be chosen** | — | Prominent action fills |
| `destructive` | system red | semantic | Quit, sign out, delete |

**Materials are the hard part.** Cabinet leans on four surface treatments that
Linux gives you nothing for:

| Cabinet's name | What it does | Linux equivalent |
|---|---|---|
| `.ultraThinMaterial` | heavy blur, very light tint | backdrop blur, ~30px, white @ 10% |
| `.regularMaterial` | heavy blur, mid tint | backdrop blur, ~40px, white @ 18% |
| Liquid Glass `.regular` | the above plus edge refraction and specular highlight | **no equivalent** |
| Liquid Glass tinted | as above, tinted white @ 22–35% | **no equivalent** |

CabinetOS should implement the first two as a real backdrop blur and **not
attempt the last two.** Liquid Glass is a system effect with per-frame cost that
Apple absorbs; an imitation of it is both expensive and recognisably not it. The
honest translation is a tinted blur, which is what Cabinet itself falls back to
on tvOS 18 and which the source describes as adequate.

### Type

The ramp is Apple's tvOS text styles. Cabinet names styles, never sizes, with
two exceptions. **These sizes are Apple's published tvOS ramp, not measured on
the device** — treat them as the intended proportions and verify once there is
something on a screen.

| Cabinet's name | Size / weight | Used for |
|---|---|---|
| Large Title | 76 bold | Game detail title; settings page titles |
| Title 1 | 57 | — |
| Title 2 | 48 bold | Shelf headers ("Recent", "Favorites"); pause-menu game name (semibold) |
| Title 3 | 38 semibold | Settings rows, tile titles, switcher pills, pause-menu buttons |
| Headline | 38 semibold | Hero card game title |
| Callout | 31 | Cover captions; secondary detail under a settings row |
| Body | 29 | |
| Footnote | 29 | Game counts on tiles |
| Caption 1 | 25 | Hero card platform label |

**One hardcoded size exists** in the whole tvOS UI — **40 bold**, the rom grid's
own screen title inside its glass chip. It sits between Title 2 and Large Title
because a grid title should not shout as loudly as a game's name does. Everything
else names a style.

One rule is recorded as a mistake already made: **shelf captions and grid
captions must be the same style.** Home's shelves ran at Title 3 while the
library grid ran at Callout, "not a deliberate size difference." Both are
Callout now.

### Spacing and sizing

Everything below is from `TenFoot` and the tvOS views, in points.

| | Value |
|---|---|
| **Content inset**, horizontal | 60 (Home) / 80 (Library, grid, detail, settings) |
| **Shelf cover** | 260 × 347 (3:4) |
| **Shelf spacing** | 40 |
| **Shelf vertical padding** | 24 — headroom for the focus scale, not decoration |
| **Caption gap** below a shelf cover | 6 |
| **Grid cover** | adaptive, minimum 260 |
| **Grid column spacing** | 48 |
| **Grid row spacing** | 44 |
| **Caption gap** below a grid cover | 10 |
| **Platform tile** | adaptive minimum 380 wide, **200 tall**, spacing 36 both axes |
| **Settings column** | max width **1100**, rows 16 apart |
| **Settings row padding** | 32 horizontal, 22 vertical |
| **Hero card** | full content width, height `min(screenHeight × 0.40, 420)` |
| **Detail cover** | 340 × 460 |
| **Pause panel** | max width 560, padding 40 |

`TenFoot` declares `gridCoverMinimum = 240` but the grid that uses it hardcodes
260. **Take 260** — the hardcoded value is the one that shipped and was looked at.

### Corner radii

There is a real system here and it is worth keeping: **radius tracks the size and
the seriousness of the thing.**

| Radius | Applied to |
|---|---|
| 8 | A cover thumbnail inside another element |
| 10 | Shelf cover art |
| 12 | Grid cover art |
| 16 | Detail-screen cover; settings rows |
| 18 | Hero card; platform tiles; pause-menu buttons |
| 32 | The pause-menu panel |
| capsule | Pills, chips, the Resume button, the library switcher |

### Focus and selection

**This is the most important part of the document.** On tvOS the focus engine is
free; on Linux it is the single biggest thing CabinetOS has to build. Cabinet's
own rule sits in its conventions file, put there after the same mistake was made
at least twice. In summary — the original is longer and names specific SwiftUI
styles:

> Never use the system's default focus treatment. It paints a solid plate over
> whatever the element already has, and it reserves no headroom for its own
> scale growth, so a focused row grows into its neighbour.

Cabinet therefore defines **three** focus treatments, and everything focusable
uses one of them.

**Reserved headroom is the other half of that rule**, and it is a layout
obligation rather than a style one: a shelf carries 24pt of vertical padding for
no reason except that its cards grow by 10% when focused, and without it the
grown card is clipped against the rail's bounds. Every container holding
focusable elements has to budget for their focused size.

#### 1. Artwork — lift, shadow, rim

For anything whose content is a picture.

| | Rest | Focused | Pressed |
|---|---|---|---|
| Scale | 1.00 | **1.10** | 1.02 |
| Shadow | none | black @ 55%, blur 26, offset y +14 | |
| Rim | none | white @ 85%, **4pt, inset** | |
| Duration | | 180 ms ease-out | 120 ms ease-out |

Two details that are not obvious and both came from real bugs:

- **Pressed scales *down* from focused**, to 1.02. The card is already raised, so
  a click has to read as a push *into* the screen or it does not read at all.
- **The caption slides down** by `coverHeight × 0.05 + 2` when its card is
  focused. A 1.10 scale about the centre advances the bottom edge by 5% of the
  height, which buries the caption underneath it. The 0.05 is half of
  `1.10 − 1`; if the scale changes, this changes with it.

The rim is **suppressed on composite elements** — anything whose label mixes art
with its own text, like a platform tile. A rectangle drawn around the whole
button always crosses the text somewhere. The scale and shadow carry focus
perfectly well alone.

#### 2. Text controls — tint, lift, a shape behind

For a short label or a pill: a shelf's "Recent ›" header, the Platforms /
Collections switcher, a save-state entry.

| | Rest | Focused |
|---|---|---|
| Text | secondary | white |
| Background | none | tinted blur, white @ 25% |
| Scale | 1.00 | **1.06** |
| Padding | 14 horizontal, 8 vertical | |
| Duration | | 180 ms ease-out |

#### 3. Rows — a surface that is always there

For a full-width settings-style row.

| | Rest | Focused |
|---|---|---|
| Background | blur, untinted | blur, white @ 22% |
| Scale | 1.00 | **1.03** |
| Radius | 16 | |
| Duration | | 180 ms ease-out |

The scale shrinks as the element grows: **1.10 for a cover, 1.06 for a pill,
1.03 for a full-width row.** That is not arbitrary — a full-width row growing
10% would collide with its neighbours, and a small pill growing 3% would not
read at all.

#### Where focus lands

- **Home puts focus on the hero** on arrival, explicitly, arbitrated against the
  account chip that sits above it in reading order.
- **Library puts focus on the switcher** on first arrival — but *only* the first
  time. Re-entering from a pushed screen must leave focus where back-navigation
  put it. Cabinet got this wrong first: forcing focus on every appearance yanked
  it away whenever the user came back from another tab.
- The primary action on a screen should be reachable without travelling
  through secondary ones.

#### Selection is not focus

A selected library switcher pill is tinted **white @ 35%**; a focused one is
tinted **white @ 25%** and scaled. The two states are independent and both are
visible at once. This matters: a controller-driven UI where the user can move
focus away from the current selection has to show both, or they lose their place.

### Motion

Cabinet's motion vocabulary is small and almost entirely ease-out. That is the
system, and it should be kept.

| Duration | Curve | What |
|---|---|---|
| **60–80 ms** | ease-out | Button press feedback; an LED changing |
| **120 ms** | ease-out | Press state on a focused card |
| **150 ms** | ease-out | Pause-menu button focus; menu appear/dismiss |
| **180 ms** | ease-out | **The focus transition. The most-used value in the app.** |
| **220 ms** | snappy | A segmented choice changing |
| **250 ms** | ease-in-out | A pairing code appearing; a progress bar |
| **280 ms** | ease-out | Launch transition, secondary elements |
| **350 ms** | ease-out | Artwork arriving asynchronously — a cover mosaic filling in |
| **350 ms** | ease-in-out | The in-game overlay appearing and dismissing |
| **600 ms** | ease-in-out | A deliberate state change the user should watch |
| **1400 ms** | ease-in-out, repeating | Boot-curtain shimmer |

Springs appear **four** times in the whole app, always for something with
physical character. Two are tuned; two use the platform default to snap a
dragged element back:

| Response | Damping | What |
|---|---|---|
| 0.34 | 0.55 | A control pad element settling |
| 0.40 | 0.42 | The launch transition's lead element — deliberately loose, so it overshoots |
| default | default | A dragged sheet returning to rest (twice) |

**Rules that fall out of this:**

1. **Ease-out is the default.** Things arrive quickly and settle. Ease-in-out is
   for a change of state the user asked for; ease-in is used nowhere.
2. **180 ms is the focus tempo**, and nothing about focus should be slower. A
   controller user crosses a shelf faster than that, and the animations must not
   queue up behind them.
3. **Asynchronous content fades in at 350 ms**, never snaps. Cover art arriving
   over a network is the common case.
4. **Springs are rare and mean something.** Everything else is a curve.

> **Phase 3 warning, already recorded:** software rendering in the VM makes
> motion choppy. **Build the motion from these numbers; do not judge it there.**
> An animation tuned against software rendering is tuned against the wrong
> feedback.

### Navigation model

**Four destinations, always reachable, in a bar across the top:**

> Home · Library · Search · Settings

Search is drawn apart from the other three — it is not just a fourth tab. Library
is **hidden entirely** when the machine is offline, rather than shown empty:
its only honest content offline is exactly what Home already shows, and *"if both
tabs are the same why two."*

Settings is a real destination on a television, not a corner button. On a phone
that placement is about thumb reach; a controller has no thumb reach and every
destination costs the same number of clicks.

Within that:

- **Each tab owns a navigation stack.** Library pushes to a platform's grid.
- **Game detail is a full-screen cover, not a push.** It replaces the screen
  entirely, with the artwork as its own backdrop.
- **The player is a full-screen cover over the detail screen.** So quitting a
  game returns to the detail screen, and backing out again returns to where the
  user was browsing.
- **No screen titles that repeat the tab.** Library has no "Library" heading;
  Settings has no "Settings" heading. The bar already says it.
- **A pushed page carries its title as ordinary content at the top of its own
  scroll view**, never as system chrome. On tvOS the system version painted over
  the artwork.

#### Home is resume-first

Home is not a menu. Its structure is fixed:

1. **The hero** — what you were playing. Focused on arrival.
2. **Recent** — everything else recently played, as a horizontal shelf.
3. **Favorites** — a second shelf, only if there are any.

The hero carries **two** actions and the distinction is load-bearing:

- **Resume** (a pill in its top-right corner) goes *straight into the game*,
  with the previous choices already made and the newest state loaded, wherever
  that state was written. Resume means resume; stopping at a screen with a Play
  button on it is two actions, not one.
- **The artwork itself** opens the detail screen, which is where you go to pick a
  different state, change the core, or export.

When there is nothing to resume, Home says so in its own words and points at the
Library — it does not show an empty shelf.

##### Resume on a game that is not downloaded — RAISED AND CLOSED, 2026-09-16

**Recorded because it looks like a problem and is not, and somebody will raise
it again.**

Recent and the hero come from RomM's `last_played`, which is the household's
history across every device. So the game Home offers to resume may have been
last played on a phone and never downloaded on this console, and pressing Resume
then means fetching several gigabytes and a save state before anything starts.

That was argued here as a broken promise — a wait behind a button whose purpose
is that there is no wait — with three proposed fixes: a progress bar inside the
pill, pre-fetching the hero while idle, and a badge saying which kind of Resume
was coming.

**Closed by MMagTech, and he is right.** You press Resume, it downloads, it plays.
The wait is the wait whichever way it is presented, and the download already
shows progress and already takes Escape to cancel. Warning someone in advance
does not shorten it and does not change what they would do — they want to play
that game.

**The rule this section states is about the number of ACTIONS, not the number of
seconds.** Resume is still one action. It is slower some of the time.

*Pre-fetching the hero while the console is idle remains available* as an
optimisation, the way real consoles have it, and it is a performance idea rather
than a correction to this design. Nothing about Home changes.

#### The hero card, read from Cabinet's tvOS source

**Read 2026-09-14 from `RommApp/RommApp/Home/HomeView.swift`.** tvOS has shipped
this and CabinetOS should inherit it rather than re-derive it. The numbers below
are that file's, and the reasoning next to them is its own.

**The tvOS composition** (`tvContent`) — this is the one to follow, not the Mac
variant:

```
VStack(spacing: 16), padding: horizontal 60, top 0, bottom 16
    hero          height = min(screenHeight * 0.40, 420), wide, padding-bottom 20
    Recent        a shelf, only when there are recents besides the hero
                  else, when loaded and there are none: the empty state
    Favorites     a second shelf, only when there are any
```

**The hero card itself:**

| Part | Treatment |
|---|---|
| Artwork | **Fitted, not filled** — box art is tall and the hero is wide, so filling slices the art to a strip of its middle |
| Backdrop | The *same* artwork, filled, **blurred 20**, with black at 15% over it — so the leftovers are the art's own colours rather than letterbox bars |
| Art inset | `padding-top 14`, keeping the fit image off the card's rounded top corners, which otherwise clip a sliver |
| Band | A **frosted material**, not a black gradient — the gradient painted over the very backdrop that makes the card worth looking at |
| Band content | Title (headline) over platform label (caption), spacing 2, padding h12 v10 |
| Band height | Computed from the two line heights + 2 + vertical padding, not hardcoded |
| Corner radius | 18 |
| Resume pill | Overlaid **top-trailing**, inset 12; capsule, ultra-thin material, play glyph + "Resume", min-width 92, padding h14 v8 |

**The two actions are load-bearing and must not collapse into one.** The pill
goes *straight into the game*, with the previous choices made and the newest
state loaded. The artwork opens the detail screen, which is where a different
state, a different core or an export is chosen. Cabinet's own comment: stopping
at a screen with a Play button on it is two actions, not one.

##### The hero height is a hard-won number, and it carries a warning

The comment above `min(height * 0.40, 420)` records the iteration, and it is
worth reading before anyone "tidies" it:

- **0.42 / 460** pushed Recent's caption past the bottom edge on a 1080pt screen.
- **0.34 / 380** *still* cut it off on real hardware.
- **0.28 / 300** fit with margin to spare.
- **0.34 / 360** left a visible gap below Recent's caption.
- **0.40 / 420** is where it landed.

The reason the second attempt failed is the part CabinetOS must take seriously:

> *"a physical TV's overscan safe area eats more vertical room than the
> simulator's raw framebuffer capture shows."*

**That is exactly the trap this project's VM is set up to fall into.** The
standing rule is "judge no motion on the VM"; this widens it. A `--screenshot`
from a software-rendered VM will overstate the vertical room available on a real
television in the same way the tvOS simulator did. **Vertical fit is not
answerable on the VM either** — only on the SER5, on a real panel.

##### The empty state is the first thing to build, because it is today's truth

Home is resume-first and nothing has ever been played, so there is nothing to
resume. Cabinet's tvOS copy, written for a television rather than reused from
the phone — its own comment notes that "on the go" means nothing on a TV:

> **Nothing to resume yet**
> Pick something from Library and it'll be here next time.

Centred, title2 bold over title3 secondary, minimum height 300. This is the
honest Home until a play history exists, and it is buildable now.

**The Mac variant differs and is not the model**: hero `min(h * 0.34, 320)`,
spacing 14, top padding 24. Cabinet's own note asks that the two be kept
structurally in step while the scale differs. CabinetOS is a ten-foot interface,
so it follows tvOS.

### Component inventory

Everything CabinetOS's frontend needs to draw, with the treatment it uses.

| Component | Shape | Focus treatment |
|---|---|---|
| **Cover card** | 3:4 art, radius 10–12, caption below | Artwork |
| **Hero card** | wide, radius 18, art fitted over a blurred copy of itself, frosted title band at the bottom | Artwork (no rim) |
| **Resume pill** | capsule, blurred fill, icon + label | Text control |
| **Shelf** | header row (title + chevron) then a horizontal rail | header is a Text control; cards are Artwork |
| **Rail edge fade** | the rail is masked to transparent over its outer 4% at each end | — |
| **Platform tile** | 380×200, radius 18, label left, cover right, blurred art behind | Artwork (no rim) |
| **Cover grid** | adaptive columns, two-line captions with reserved space | Artwork |
| **Switcher** | a row of capsule pills, one selected | Text control |
| **Screen-title chip** | a static glass capsule, not a button | — |
| **Settings row** | full width, title + detail + optional value + chevron | Row |
| **Settings page** | plain large title, then rows, max 1100 wide | — |
| **Pause menu** | scrim, centred panel radius 32, full-width buttons | its own — scale 1.04, 150 ms |
| **Primary action button** | the one place a solid opaque fill is right | platform default |
| **Badges** | small overlays on a cover: incompatible, favourite, downloaded | — |
| **On-screen keyboard** | *does not exist in Cabinet* — tvOS provides one | **CabinetOS must build this** |
| **Progress** | a determinate bar for downloads; the label carries the percentage | — |
| **Toast / banner** | capsule, blurred, slides in from the top edge, self-dismissing | — |

Two rules about lists worth carrying:

- **A list of covers gets two caption lines with reserved space**, so rows stay
  aligned whether a title wraps or not. One line truncated almost every real
  title at these widths.
- **A wide screen does not get a full-width row.** A row stretched to 1920
  points leaves a name at the far left and a count at the far right with a third
  of the screen empty between them. Use a tile grid, which also gives the focus
  engine a real two-dimensional field to move in.

### What CabinetOS has to build that Cabinet got for free

Stated plainly, because it is the honest cost of "owned, not skinned":

1. **A focus engine.** Spatial navigation between arbitrary rectangles, with
   remembered focus per container, and the three treatments above.
2. **An on-screen keyboard.** The baseline for all text entry, per the input
   model. Cabinet never wrote one.
3. **Backdrop blur** as a real, cheap effect, since it is load-bearing in almost
   every component.
4. **Asynchronous image loading** with the 350 ms fade, placeholder handling and
   a memory budget.
5. **Safe-area handling** for overscan.
6. **The text ramp**, as actual numbers, with a font chosen and shipped in the
   image.

None of these is research. All of them are work, and they are the reason Phase 3
is a phase.

---

## The frontend toolkit

**Recommendation: C++20, SDL3 for platform and input, one EGL / OpenGL ES 3.x
context, and a hand-written retained UI layer. Evaluate RmlUi for the UI layer
before writing one.**

Decided in Phase 0 with Part 1's findings in hand. Record a reversal here rather
than editing this, if it is reversed.

### What the program actually is

The constraint that decides this is in *Emulation*, and Part 1 sharpened it:

> The frontend is not a launcher. It owns the frame loop, loads cores as
> libraries, feeds them ROM data and controller input, presents their output,
> and draws its own UI over the top — **in the same graphics context**, at a
> stable 60 Hz, while a PS2 is being emulated underneath.

Six hard requirements fall out, and each one eliminates candidates:

1. **Cheap C FFI, called per frame.** `retro_run` is called up to twice per
   draw; `retro_serialize` moves megabytes. Any toolkit whose foreign-function
   boundary has per-call overhead or a marshalling step is disqualified.
2. **A real GL context the frontend owns and hands to cores.** Flycast,
   Mupen64Plus and PPSSPP render through
   `RETRO_ENVIRONMENT_SET_HW_RENDER` into an FBO. The toolkit must let the
   frontend create that context, not create one for it and hide it.
   **Settled 2026-09-16: SDL3 plus one EGL/GLES 3 context does exactly this,
   and both requirement 2 and requirement 3 are now running rather than
   argued** — Mario Kart 64 and Ikaruga play in the same context the UI draws
   in, with no readback anywhere.
3. **The UI must draw into the same context.** This is what deletes the
   `glReadPixels` readback — the largest per-frame cost on Apple's three
   heaviest cores. A toolkit that composites the game as a separate surface or
   texture handoff gives that cost straight back.
4. **Frame pacing under the frontend's control**, to the precision the
   accumulator and the audio governor need. A toolkit that owns the render
   thread and decides when frames happen is fighting the one thing this program
   must get right.
5. **A Wayland client**, with no X11 assumption, since the session is gamescope
   or cage.
6. **Embeds two large C++ emulators.** Dolphin and PCSX2 are not libretro cores;
   they are whole emulators with host layers Cabinet has already written —
   `CabinetDolphinHost.cpp` (574 lines) and `CabinetPS2Host.cpp` (811 lines),
   plus their bridges. **Both are plain, portable C++ today.**

### Why C++

Because it is the language the program is already written in.

Cabinet's frontend is Objective-C++ whose Objective-C surface is a thin veneer:
of `LibretroFrontend.mm`'s 2,281 lines, **125 touch an Apple type, and 60 of
those are in the wrapper at the bottom.** The C++ underneath — environment
callback, video refresh, readback ladder, input state, option handling, the
core-tolerates-deinit table — ports substantially unchanged. The GL path is
**already EGL and GLES3**, behind the `CABINET_ANGLE` flag, because the Mac
build reaches GLES through ANGLE. On Linux that is the native path.

The two heavy emulators' host layers compile as they are. Every core is C or
C++. Every other emulator project that hosts cores in-process — RetroArch,
Dolphin, PCSX2, Flycast — is C++ with a hand-written renderer. Choosing anything
else means writing and maintaining a binding layer across the hottest boundary
in the program, forever, for a UI convenience.

The counter-argument is real and should be stated: **the UI is the majority of
Phase 3's work, and C++ gives you none of it.** That is true. It is also true in
every other candidate, because the thing that would have saved the most work —
tvOS's focus engine — has no equivalent anywhere. See the alternatives below.

### The shape to build

**One process. One EGL context. Cores as `.so` files.**

```
cabinetos-frontend  (C++20, SDL3, EGL/GLES3)
  ├── libretro host      — the ported LibretroFrontend, dlopen + RTLD_LOCAL
  ├── UI layer           — focus engine, layout, text, blur, OSK
  ├── RomM client        — REST + WebSocket
  └── session            — storage, saves, sync queue

/usr/lib/cabinetos/cores/
  ├── flycast_libretro.so
  ├── mgba_libretro.so
  ├── … 21 cores
  ├── dolphin.so         — Dolphin + CabinetDolphinHost, behind the same struct
  └── pcsx2.so           — PCSX2 + CabinetPS2Host, likewise
```

Three consequences, all good:

- **The symbol-prefixing apparatus disappears.** `RTLD_LOCAL` gives namespace
  isolation for free, so `bsat_wrapper.c`, the `ld -r` merges, the exported
  symbol lists and `-fno-common` all go. See open question 13.
- **Dolphin and PCSX2 need not be linked into the frontend.** Put each behind the
  same struct-of-function-pointers the libretro cores use, compile its host layer
  into its own `.so`, and the frontend binary stays small and fast to rebuild —
  which is what makes the SFTP development loop bearable.
- **Separate `.so` files rechunk into smaller image layers**, which the update
  model cares about: a core bump moves one layer, not the whole image.

`SDL3` covers Wayland, gamepads (including hotplug and the Steam-style mappings
Bazzite already ships udev rules for), and audio. It is already in the base
image. Use it for platform, input and audio; **do not** use its renderer — the
frontend needs the raw GL context.

Audio goes to PipeWire through SDL3, with the same rule as Cabinet: **the
callback never blocks.** It drains a ring the draw loop fills. That rule is what
made the audio governor necessary, and it is the right rule.

### The UI layer

The design system section is the specification for this. Before writing it from
nothing, **evaluate RmlUi**: it gives layout, text shaping, and a CSS-like
styling system, and — decisively — it renders through a backend *you* supply, so
it shares the frontend's GL context rather than owning one. That is the property
that matters, and most UI libraries do not have it.

Dear ImGui is the other option and has direct precedent: **PCSX2's own
`FullscreenUI` is a controller-driven, cover-art, ten-foot console UI built on
ImGui, running over a live emulator.** Cabinet's PCSX2 patch #13 disables it
precisely because Cabinet has its own — which is a demonstration that the
approach works, from inside this project's own dependencies. The reservation is
that immediate mode makes remembered focus, caption slides and interruptible
transitions awkward enough that a retained layer tends to get built on top
anyway.

Either way, **the focus engine is ours.** Nothing provides it.

### The alternatives, and why not

| | Why it was considered | Why not |
|---|---|---|
| **Qt 6 / QML** | The strongest alternative. Gives layout, text, a focus and key-navigation model, shader effects for blur, and animation declared exactly as the design system states it (`easing.type: Easing.OutQuad; duration: 180` maps one to one). Qt 6 is **already in the base image** — see open question 1. | Qt Quick's scene graph owns the render thread and its vsync cadence, which is the one thing requirement 4 says must be ours. It is injectable (`QSGRenderNode`, `beforeRendering`) but you are then fighting the framework at the hottest point in the program. Qt Virtual Keyboard is GPL-or-commercial. **Worth a spike before committing against it** — see below. |
| **Rust + wgpu** | Good FFI to C, strong tooling, memory safety where it is genuinely useful. | `wgpu` abstracts away the GL context the cores require, so you would run raw EGL beside it and share textures across two graphics abstractions. C++ interop (Dolphin, PCSX2) needs a C shim — smaller than it sounds, since Cabinet already wrote those bridges, but real. And this project has one developer, who is not a Rust developer. |
| **Godot** | Owns a frame loop, has a UI system with focus neighbours, exports to Linux/Wayland. | Owns the frame loop *its* way. Embedding a core's GL FBO into its renderer, and embedding Dolphin and PCSX2 at all, is fighting the engine. Ships a large runtime to draw ten screens. |
| **GTK4** | In the image already. | A desktop application toolkit. No ten-foot story, no focus model of the kind needed, and the same render-loop ownership problem without Qt's compensating strengths. |
| **Flutter** | Real Linux embedder, decent FFI. | The game reaches the screen through the texture registry — a handoff, which is requirement 3 given straight back. Desktop-oriented. |
| **Electron / web** | — | A browser is a non-goal, and this is the frame loop of an emulator. |
| **RetroArch, EmulationStation, ES-DE** | Solve much of this already. | Constraint 4: the frontend is owned, not skinned. Also none of them hosts real PCSX2 and Dolphin in-process, which is the thing that makes one overlay and one save-state path possible. |

### The one spike worth running before Phase 3 starts

Qt is the only alternative strong enough to be worth an hour of doubt, and the
question between it and C++ is narrow and testable:

> **Can a Qt Quick scene draw over a libretro core's FBO, in the same GL
> context, with the frame loop paced by us rather than by the scene graph?**

Build one screen — a shelf of covers with the three focus treatments — over
Flycast, in the VM, under cage. If the pacing is ours and the readback is gone,
Qt buys a great deal of Phase 3 for free and the base image already carries it.
If it is not, the answer is C++ and the hour was worth it.

Do not run this spike for Godot, Flutter or Rust. Their objections are
structural, not empirical.

### What this decides about the image

- **Runtime dependencies stay small**: SDL3, Mesa (EGL/GLES), PipeWire, and
  whatever the UI layer needs for text. All already present.
- **Open question 1 reopens slightly.** If the spike chooses Qt, Plasma's Qt 6
  is load-bearing rather than dead weight and the question answers itself. If it
  chooses C++, Qt has no user in the image and the removal argument gets its
  first real reason beyond tidiness.
- **`ci/base-watch.txt` gains** SDL3, Mesa and PipeWire, per *Staying current
  with Bazzite*'s own instruction that the list must grow with the project.

---

## Licensing, and the constraint it puts on the project

**`docs/LICENCES.md` is the list.** This section is the part that shapes
decisions rather than the part that credits people.

**Six of the twenty-one cores are free for NON-COMMERCIAL use only** — FBNeo,
MAME 2003-Plus, Snes9x, Genesis Plus GX, PicoDrive and Opera. That is not GPL
and it is stricter: GPL restricts the terms you distribute under, these restrict
whether you may sell at all. Between them they cover Arcade, SNES, Genesis, Sega
CD, Master System, Game Gear, 32X and 3DO.

> **CabinetOS is free, is not sold, and takes no donations. That is what keeps
> those cores legitimate, and it is a deliberate constraint rather than an
> oversight.** Cabinet states the same thing about itself, and **the risk is
> sharper for an operating system**: an app is hard to accidentally sell, and an
> OS is something you put on a box. The moment money changes hands for a machine
> carrying this image, those six cores have to come out or be relicensed.

**This has teeth for decisions already on the table.** *Hardware* says the
reference machine may change and has already changed once. If it ever changes
into a product, this is the constraint that bites first — before performance,
before storage, before anything discussed in open question 12.

**Everything else of consequence is GPL**, which is not a restriction on use:
anyone may run, study, modify and redistribute it, commercially included. The
obligation is on distribution — pass on the same freedoms and make the
corresponding source available. CabinetOS `dlopen`s its cores rather than
statically linking them, which is a looser coupling than Cabinet's build, but
the conservative reading is the same: the image is a combined work. That is
satisfied the way Cabinet satisfies it — every core is built from a named
upstream commit by a script in this repository, and the three in-flight source
patches are visible in that script rather than vendored.

**Two things are owed and neither exists yet:** the licence text readable on the
console itself (Settings → About, per *Branding*), and a verification pass over
each licence line against the source it came from, since those were carried
across from Cabinet's list rather than checked here.

---

## Non goals

- Not a Steam machine.
- Not a general purpose desktop.
- No app store.
- No browser.
- Nothing that makes it a computer instead of a console.

The one sanctioned exception is **developer mode**, above: hidden, opt-in, and
invisible to anyone who has not deliberately turned it on.

---

## Constraints and principles

1. **Stay at the application layer.** No custom kernel modules, no hardware
   hacks. Those are what make upstream changes dangerous, and the whole point of
   basing on Bazzite is to let someone else own the kernel and driver problem.
2. **The Bazzite base is pinned to a specific tag and digest**, and only moved
   deliberately, as its own commit, with a VM boot test. Automation proposes
   base bumps and explains what changed; it never merges one. See *Staying
   current with Bazzite*.
3. **Anything that could leave the user stuck at a terminal is a bug.**
4. **The frontend is owned, not a skin** on someone else's frontend.
5. **Removals are conservative.** When it is not clear that a Bazzite package is
   safe to remove, it stays in the image and goes in Open questions. A slightly
   larger image costs nothing. An image that does not boot, or that boots
   without controller support, costs a reflash — and eventually costs the
   ability to trust the base at all.
6. **Builds happen in CI, on Linux.** The project is developed on a Mac, which
   cannot build or run bootc images. Nothing in this repo may depend on being
   able to build locally.
7. **One branch at a time, and the pull request is aimed at `main`.** Four
   branches were once stacked on each other here, each opened before the last
   had merged, and the result was three overlapping pull requests and a compile
   check that did not apply to any of them.

   **THE HANDOVER IS NOT AN EXCEPTION TO THIS, IT IS PART OF THE WORK.**
   Decided 2026-09-19, after `docs/NEXT-SESSION.md` was pushed straight to
   `main` at the end of a session and nobody could say afterwards whether that
   was allowed. It is not, and it never needs to be: **the handover is written
   inside the pull request that does the work it describes**, so there is never
   a handover-only push and never a handover-only pull request. A session that
   produced nothing to merge has nothing to hand over either.

   The reason is not tidiness. `NEXT-SESSION.md` is the most load-bearing
   document in the repository — it is the only thing a fresh assistant reads
   before touching anything — and a wrong sentence in it costs a day. It
   deserves the same review surface as the code, and it is easier to write
   honestly next to the diff than from memory afterwards.

   **The one narrow exception**, and it has to be narrow or it eats the rule: a
   fact that cannot be known until after the merge — the pull request's own
   number, the merge commit — may be corrected straight on `main`, in one
   commit that changes nothing else.

---

## Phase plan

### Phase 0 — Design system spec
**Status: COMPLETE, 2026-09-13.**

*Done when* a developer who has never read a line of Swift could reproduce the
look and feel of Cabinet from the document alone.

**Shipped, all from reading Cabinet's source rather than its documentation:**

- ***The design system*** — the canvas, colour tokens, the type ramp, spacing,
  corner radii, the three focus treatments, the motion vocabulary, the
  navigation model, and a component inventory. It ends with an explicit list of
  the six things tvOS provided free that CabinetOS has to build.
- ***How Cabinet hosts cores*** — the ROM's path from RomM into a running core,
  the three directories and why confusing them lost saves, how in-game saves
  differ from save states, where a state lives before and after upload, the
  frame loop and the audio governor, the two video paths, and the overlay's
  input-mode rule.
- ***The frontend toolkit*** — the recommendation, the requirements that produce
  it, the alternatives, and the one spike worth running before Phase 3 starts.
- **Open question 13, scoped** — every core checked rather than assumed.

**What it changed:**

- **The palette was verified and is correct**, with one missing gradient stop
  added — but this document's claim that it is what the app looks like was
  wrong. Cabinet has no colour assets at all. Corrected in *Branding*.
- **The core count was wrong.** 23, not 25, and two of those are iOS-only by
  decision, so CabinetOS carries 21.
- **A Linux core build is much cheaper than feared**, and the Apple-only
  apparatus disappears rather than being ported.
- **The parity risk moved.** It is not "can the cores be built" — it is that a
  plain Linux build silently selects a *different CPU backend*, and that
  Cabinet's build system cannot currently be run by anyone. Both have concrete
  fixes, recorded in open question 13.
- **There is a one-evening test that answers the save-state question** on
  hardware the project already owns, with no Linux toolchain. It is the highest
  value work available right now. See open question 13.

### Phase 1 — Base image
**Status: COMPLETE, 2026-09-13.**

A `Containerfile` from a pinned Bazzite tag, with the desktop and Steam removed
and the gaming stack retained. GitHub Actions builds, signs and publishes to
GHCR, and produces a bootable disk image.

*Done when* CI is green and the image boots in a VM to a console prompt.

*Shipped so far:* repository scaffold, `Containerfile` pinned to
`ghcr.io/ublue-os/bazzite:stable-44.20260908`
(`sha256:437920bae6935fd70719c1e0109f3469b1215a788330b0de924d0c7ac8aaa84c`),
strip scripts, SSH enabled for development, build/sign/push workflow, disk image
workflow, and the Bazzite base-update watcher.

*First green build: 2026-09-13.* `ghcr.io/mmagtech/cabinetos:latest`, public and
pullable, unsigned (no `SIGNING_SECRET` set yet).

What the build proved:

- Controller support survives the Steam removal — open question 2, now resolved.
- `gamescope` is present, at `/usr/sbin/gamescope` rather than `/usr/bin`.
- The default target is `multi-user.target`, so the image boots to a console.
- `openssh-server` was already in the base; `sshd.service` is enabled.

What it also showed, which is worth knowing before anyone reads too much into
the strip scripts: **the base went from 2745 packages to 2705.** Forty packages.
Most of the desktop application list was never installed on this base at all —
no Firefox, no Discover, no Okular, no GNOME anything — and Plasma itself is
untouched by design. The desktop is *unreachable*, not absent. See open
question 1.

The display manager here is `plasma-login-manager`, not `sddm`. Listing both was
the right call.

**Status changed to COMPLETE 2026-09-13.** A qcow2 was built, booted in an
Unraid VM, and reached a console prompt. SSH works. The machine has since been
upgraded in place with `bootc upgrade`, which pulled 1.0 GB of a 5.0 GB image
because only changed layers moved — the Phase 7 update mechanism working, months
early.

An `anaconda-iso` also builds. It has **not** been booted; installing to real
hardware is the one step in the chain never exercised.

### Phase 2 — Boot to frontend
**Status: in progress. The session works and now launches the real frontend;
splash and power button remain.**

Autologin, no display manager, a custom session launching a fullscreen
application. Every route to a desktop, file manager or terminal closed.
Shutdown and suspend reachable from a controller.

A keyboard and mouse attached to the session must work — they simply must not be
needed. Closing "every route to a terminal" means the UI offers none, not that
input devices are blocked.

*Done when* power on leads to the frontend with no keyboard involved.

**Shipped:** `cabinetos-session.service` takes tty1 via
`Conflicts=getty@tty1.service`, so there is no login prompt to fall back to.
`PAMName=login` gives it a real logind session on seat0. `StartLimitIntervalSec=0`,
because a console that has given up retrying is a support call.
`/usr/bin/cabinetos-session` works down the compositor ladder (gamescope/drm →
cage → headless). The session user is created by the image via `sysusers.d`.
`plasma-setup` and the `plasma.desktop` session are gone. Seven dead-weight
services disabled — 91 MB and 5 seconds of boot.

Verified on the VM: session active, zero restarts, correct fallback chosen.

**AND IT RUNS THE FRONTEND AS OF 2026-09-19**, rather than `sleep infinity`.
That line was written as a Phase 2 placeholder with a comment saying Phase 3
would replace it, and it outlived Phase 3 entirely: the frontend existed, ran,
played games and synced saves for a fortnight while the image still shipped
the placeholder, because the image did not contain the frontend to run. Phase
5, *The deploy*. `CABINETOS_APP` still overrides it, which is how the VM points
the session at a build it has just compiled.

**Remaining:**

1. **Boot splash.** Design settled — see *Branding*. Needs `quiet loglevel=0` on
   the kernel command line, a Plymouth theme, and a handoff to the frontend with
   no black flash. Testable in the VM; Plymouth's renderers do not need Vulkan.
2. **Power button → clean shutdown.**
3. **Re-verify on a freshly installed image**, rather than one upgraded in place.

### Phase 3 — Frontend shell
**Status: in progress. The foundation runs on the VM, 2026-09-13.**

**Shipped:** `frontend/` — C++20, SDL3, one EGL/GLES 3 context, and a UI layer
of our own, built in a pinned Fedora 44 container (the image carries no
compiler, deliberately) and run as an ordinary Wayland client of the session's
existing `cage`. No toolkit, no Qt, no spike needed — see *The frontend
toolkit* for why that argument resolved without one.

Running and verified on the VM:

- A real GLES 3.2 context under cage, on llvmpipe, 1280×800.
- The design canvas, letterboxing correctly on a 16:10 panel, and **identical
  layout at 4K, 1080p and 720p** — measured, see *The canvas*.
- The **artwork focus treatment, to the point**: 1.10 scale, a 4pt inset white
  rim at 85%, a black 55% shadow blurred 26 and offset 14 down, the press state
  pushing back to 1.02, and the caption sliding clear of the grown card.
- 180 ms ease-out, retargeting from the current value on interruption rather
  than jumping — the reference implementation's own animation behaviour.
- Controller and keyboard both driving focus, neither required.

**Text, added the same day.** FreeType rasterises glyphs on first sight into one
greyscale atlas; strings are drawn as textured quads on the same path cover art
and a running core's frame will use.

- **The font is Noto Sans**, Regular / Medium / SemiBold / Bold, **already in
  the Bazzite base**, so the type ramp costs the image nothing. `ci/base-watch.txt`
  should gain `google-noto-sans-fonts`, since the frontend now depends on it.
- **Noto Sans CJK is the fallback**, also already present. A ROM library is full
  of Japanese titles and Noto Sans has no CJK coverage; a missing glyph walks
  the fallback list rather than drawing a box. Verified with a Japanese title in
  the shelf.
- **Glyphs are rasterised at device pixels and laid out in design points.** A
  31pt caption is 31 pixels tall at 1080p and 62 at 4K, and the atlas is keyed
  by device size, so the same label at two scales is two entries. Rasterising at
  the design size and letting the GPU scale would make 4K text a blurry upscale
  of 1080p text, which is exactly what a console must not look like. **Verified:
  1.0 design point of edge softness at both 4K and 1080p** — an upscale would
  show two.
- **Baselines snap to a device pixel.** A baseline landing on a half pixel makes
  a whole line slightly soft, which on a television reads as cheap rather than
  as antialiasing.
- Long titles truncate with a real ellipsis (U+2026, one glyph).

**Cover art, added the same day.** libjpeg and libpng — both already in the
base, so still nothing bundled and nothing vendored.

- **Decoding never touches the frame thread.** Four worker threads decode; the
  GL upload happens on the frame thread because GL is not thread-safe; a cover
  that is not ready simply is not drawn yet. A shelf is dozens of JPEGs and
  decoding one on the thread that drives a core is how a console stutters.
- **There is a memory budget, and it is tested.** 192 MB by default, evicted
  least-recently-used. **Verified**: with a 1 MB budget the covers scrolled past
  are evicted and the ones on screen are kept, so resident settles at the
  working set rather than at the budget. That is deliberate — **the budget is a
  target, not a hard cap**, because flashing a blank card to honour a number is
  the wrong trade.
- **Where the bytes come from is one `std::function`.** Files today,
  authenticated RomM requests in Phase 4, and the cache never learns what a
  server is. Keys are not paths — everything after `#` is stripped — so a real
  URL with a query string is already the shape it expects.
- **Odd-shaped covers are fitted over a blurred echo of themselves**, never
  cropped, with a black 18% scrim between. That is the reference
  implementation's own rule and its own threshold: more than 0.06 away from 3:4.
  The echo is a high mip level sampled back up and scaled 1.3, which is a box
  blur for the price of a texture fetch rather than a blur pass.
- **Everything is clipped to the card's rounded corners** — the echo included,
  which is why the clip rectangle is a separate thing from the drawn rectangle.
- Format is detected from magic bytes, never a file extension: a server hands
  you a content type and a body, not a filename. WebP is next when something
  needs it; libwebp is already there.
- A malformed image cannot take the console down. libjpeg's default error
  handler calls `exit()`; this one does not.

**The first core runs, 2026-09-13.** Gambatte, and **Dr. Mario boots**.

This is the first empirical evidence for anything in open question 13, which
until now was entirely read rather than run:

- **Built at the manifest's pinned commit, `platform=unix`, first attempt, zero
  patches.** No source edits, no flags beyond the default, no workarounds. The
  Linux path is as easy as the Makefiles said it was.
- **The prefix-and-merge apparatus really is unnecessary.** `dlopen` with
  `RTLD_LOCAL`, resolve the `retro_*` entry points, done. No wrapper, no
  `ld -r`, no exported-symbol list, no `-fno-common`.
- **`cores/build-core.sh` is reproducible and was proved so** — the checkout was
  deleted and the whole thing re-run from nothing. git runs on the host, the
  compile runs in the builder container, and the pinned commit is checked out
  and then **asserted**, which is the discipline Cabinet's own scripts lack.

Reported by the core itself: `Gambatte v0.5.0-netlink`, 160x144, **59.7275 fps**,
32768 Hz, aspect 1.1111.

- **Frame pacing is wall-clock, not per-draw**, with at most two catch-up frames
  and the accumulator capped at four intervals. **Verified**: 96 emulated frames
  against 59.7275 fps is 1.607s, and 53,087 audio frames against 32,768 Hz is
  1.620s — the two agree to within 1%, which is the check that says emulated
  time is advancing at the rate the core asked for.
- **Audio goes to PipeWire through SDL3**, pushed from the frame loop. There is
  no audio callback at all, so there is nothing that can block — which is the
  rule the reference implementation had to work to keep.
- **The picture is integer-scaled and nearest-neighbour.** A Game Boy is 160x144
  and every pixel was somebody's deliberate choice in 1989; scaling by 6.4 makes
  some of them twice the size of their neighbours, which is visible from a sofa.
  Phase 8 can offer the smooth option; the default should be honest.
- **The overlay is drawn straight over the game** — a scrim, a panel, text. No
  compositing trick, no second surface. That is the payoff of hosting cores in
  process, exactly as *How Cabinet hosts cores* predicted.
- **`SET_HW_RENDER` is refused, deliberately and out loud.** Three cores in the
  set want it and none of them is this one; pretending would hand a core a
  context that does not exist.

**A bug worth remembering**, because it cost the first run: `dlerror()` clears
itself on read, so `dlerror() ? dlerror() : "..."` returns null the second time,
and assigning null to a `std::string` segfaults. A perfectly clear "file not
found" became a crash with no message. Read it once.

**One VM artefact, not a fault:** on llvmpipe the frontend cannot draw fast
enough, so the accumulator discards the time it cannot use and the game runs in
slow motion rather than sprinting to catch up. That is the designed behaviour
and the right one; it will not happen on a GPU.

**Seeing it on the actual screen.** On the VM the frontend runs as the session's
app via `CABINETOS_APP`, which is the hook Phase 2 left for exactly this, so
the VM boots to a build it has just compiled rather than to whatever is in the
image. **On a real console it needs no override**, as of 2026-09-19: the
session defaults to `/usr/bin/cabinetos-frontend`, which the image now carries.

It can also **photograph itself on demand, without stopping**:

```
kill -USR1 $(pgrep -f cabinetos-frontend)   # writes /tmp/cabinetos-frame.bmp
```

That is not a debugging convenience. The test VM has no way to show a person a
picture, and the machines that matter later are in other people's living rooms,
where "send me a photo of the telly" is the whole bug-report channel — see
*HDMI-CEC will not be tested by the author*, which has this problem already.

A latent bug found on the way: the session expands `CABINETOS_APP` **unquoted**,
so a path containing a space is word-split. RomM filenames contain spaces
constantly. Worked around with a wrapper script for now. **It is deliberate
now rather than latent**: the word-splitting is what lets `CABINETOS_APP` carry
arguments, which is most of what the override is for, and the comment in the
session script says so and says a path with a space needs a wrapper. The
default path has no space in it.

**Save states work, 2026-09-13.** `retro_serialize`/`retro_unserialize` wired
up, plus save RAM and arbitrary memory regions (the Game Boy clock lives in one
of its own, and saving only the save RAM loses the clock Pokemon Gold and Silver
depend on).

Dr. Mario's state is **26,882 bytes**. Verified by round trip: save, run 300
frames, restore, run 300 frames, compare.

- **The machine restores exactly.** Video bit-identical across 300 frames.
- **Restoring is deterministic.** Restore twice and both runs agree on video
  *and* audio, which is the control that separates "the restore is wrong" from
  "the path taken to get there was different".
- **Loading a state produces an audible click** — a transient at the seam rather
  than lost state. RetroArch mutes briefly after a load for exactly this reason
  and so should we.
- **Save RAM: Dr. Mario has none**, and that is unremarkable — its cartridge
  has no battery. **Save states are a separate mechanism and do not depend on
  it**: Cabinet offers them from the pause menu for these games exactly as it
  does for any other, and so must CabinetOS. This document briefly framed the
  absence as a finding; it is not one.

#### The test was wrong three times before it was right

Worth recording, because the mistake is easy and the failure mode is a test that
passes while proving nothing.

1. **It compared video on a static title screen.** Dr. Mario's title is one
   unchanging picture; it matches itself no matter what the machine is doing. The
   first version reported PASS on that. **A determinism test must first prove
   that the thing it measures actually varies** — this one now asserts the
   picture moves before trusting a video comparison, and says so in its output.
2. **Then it blamed audio residue, then the resampler's filter history, then
   `retro_serialize` having a side effect.** All three were wrong, and each was
   killed by a control run rather than by reasoning.
3. **The actual answer was that the game is silent.** Traced second by second:
   Dr. Mario untouched plays a ding at one second, a blip at nine, and nothing
   for the next twenty-five. Every "audio differs" reading had been a comparison
   of silence against a click.

The test now reports **INCONCLUSIVE** when there is no audio to compare, rather
than reporting a difference. A test that cannot tell "no signal" from "signal
differs" is worse than no test, because it is believed.

#### And then it was proved against Cabinet's own build

Run the same day, at MMagTech's insistence that reasoning about this was not
enough. **The headline result of the project so far.**

Cabinet's macOS Gambatte is pinned to `d9d6cd06` — **the same commit CabinetOS
built for Linux**. So the comparison is clean: one revision, two architectures,
two operating systems.

`tools/state-probe.c` is the instrument. One C file, no dependencies beyond
`dlfcn` and `libretro.h`, **compiled from identical source on both machines** so
that the harness cannot be the variable. It runs a core from boot with no input,
serializes, and can load a state from the other side and run on.

| | |
|---|---|
| macOS **arm64**, 1500 frames | video `dbaef6ffc1e259e7`  audio `a279cfa152a8a553` |
| Linux **x86-64**, 1500 frames | video `dbaef6ffc1e259e7`  audio `a279cfa152a8a553` |

**The emulation is bit-identical across architectures** — every pixel and every
sample, for twenty-five seconds. That was not a foregone conclusion and it is
the foundation everything else rests on.

Then the cross-load, which is the actual question:

| | 300 frames after loading |
|---|---|
| Linux x86-64 loading the **Mac** state | `e2cf3f73bd7379b1` |
| macOS arm64 loading the **Linux** state | `e2cf3f73bd7379b1` |
| macOS loading its **own** state (control) | `e2cf3f73bd7379b1` |

> **Save states are portable between Cabinet and CabinetOS.** Loading the other
> platform's state is indistinguishable from loading your own. The control run
> is what makes that a result rather than a coincidence.

One curiosity, harmless: the two state files are the same size but **23 of
26,882 bytes differ** — raw pointer values Gambatte writes into the state
(`0x000188d0cc30` on Mac against `0x04ab1c48` on Linux, right after the `dmgpal`
label). They are written and never meaningfully read, so they change nothing.
Worth knowing because **a byte-comparison of two states is NOT a valid parity
check** — these would fail it while being perfectly compatible.

#### The cores know their own revision, if you let them

Found while comparing the two builds. The Mac core reported itself as
`Gambatte v0.5.0-netlink d9d6cd0`; the Linux one, built in the container, said
only `Gambatte v0.5.0-netlink`.

The Makefile does:

```
GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
```

and compiles it into the string the core returns from `retro_get_system_info`.
**Build without git on PATH and the core forgets which revision it is.** The
builder container had no git; it does now, and the Linux core reports `d9d6cd0`
like the Mac one.

**This is the answer to "the `emulator` tag carries no version."** A core that
self-identifies can be checked at load time against the manifest, and a mismatch
becomes a refusal rather than a save state that silently will not load. Two
things follow, both cheap:

1. **The frontend should assert** the loaded core's reported version against the
   manifest, and say so loudly when it does not match.
2. **Cabinet should put the revision in the RomM `emulator` tag**, so a state
   carries the identity of the thing that wrote it. That is a Cabinet-side
   change, since Cabinet writes the tag.

Not every core does this — it needs checking per core, the same way everything
else in this question did.

#### What this still does not prove

- **One core of twenty-one**, and the easiest one: Gambatte has no recompiler at
  all, so there is no CPU-backend variable to get wrong. **The backend-sensitive
  cores are untested** — pcsx_rearmed, melonDS, Flycast and picodrive, where the
  Linux default differs from Cabinet's build. That is still the real risk.
- **macOS arm64 against Linux x86-64.** Cabinet's *tvOS* build is a third thing,
  and its Gambatte revision is one of the eleven that are unrecoverable.
- Same flags on both sides, every core option left at its default.

**The on-screen keyboard, and frosted glass with it, 2026-09-13.**

The keyboard is the gate on everything downstream: first-run setup cannot be
reached without it, and it is also the answer to the Wi-Fi question (open
question 17). **There was nothing to copy** — tvOS supplied Cabinet's, so this
is designed from the input model rather than ported.

- Digits on their own row rather than behind a shift layer, because a Wi-Fi
  passphrase is usually being read off the underside of a router.
- **Vertical movement keeps the horizontal POSITION, not the index.** Rows have
  different key counts and widths, so moving down from `p` lands near `l`
  rather than on whatever happens to be ninth.
- **Horizontal movement wraps; vertical clamps.** This reverses an earlier
  decision recorded here, and the earlier one was wrong. "No wrapping" is right
  for a shelf, where the next item is a different game and landing somewhere
  unexpected loses your place. It is wrong for a keyboard, where the grid is
  twelve wide and every key is equally somewhere you might have meant: without
  it, `1` to `del` is eleven presses instead of one. **PlayStation and Xbox both
  wrap their keyboards**, and that is the reason. Vertical still clamps — five
  rows is short enough to cross directly, and wrapping from the space bar up to
  the digits would skip the letters, which is where somebody pressing up is
  almost always going.
- Shift is one-shot, the way a phone keyboard behaves.
- Backspace steps over a whole UTF-8 code point, not a byte.
- The field shows the **tail** when it overflows: what you are typing is at the
  end, and a field that scrolls off the right hides the character just pressed.
- **A physical keyboard types into the same field**, through the same string and
  the same commit. Not a second path.
- The button legend is on screen. A controller-only UI has to say what the
  buttons do, because there is no convention to fall back on and no pointer to
  explore with.
- Per-field shortcut keys, because reducing typing beats speeding it up.

**The layout is a fixed 12-column grid**, every row totalling exactly twelve
units. The first version sized the panel to its widest row — a long function row
— and left the letter rows short, so a third of the panel sat empty beside the
letters. It looked unfinished and it wasted the one thing a ten-foot keyboard is
short of, which is reach.

**Consulted rather than invented.** PlayStation, Xbox and Steam all converge on
the same thing, and it is not a clever layout: it is a **real keyboard's own
geography**.

| | |
|---|---|
| Backspace | right end of the number row, where a real keyboard's backspace is |
| Shift | bottom-left of the letter block, where a real keyboard's shift is |
| Space | spanning the bottom, with the commit key at its right |

None of that is decoration. Somebody hunting for backspace looks top-right
*before* they read anything, and a layout that rewards the guess is faster than
one that has to be read first. The familiar arrangement is the optimisation.

The bottom row absorbs whatever is left over, so the grid stays square whatever
a field asked for — a password field passes no shortcut keys and simply gets a
longer space bar.

**Verified** inside the 60pt safe area at 1920x1080: margins 62 left, 360 right,
168 top, 140 bottom.

**And backdrop blur, finally.** It was on the list of six things tvOS gave
Cabinet for free and it is the largest of them. The keyboard is what forced it:
a translucent panel over cover art without blur is not "less pretty", it is
**unreadable** — the art shows through and competes with the text. First version
proved it.

The mechanism: the scene is drawn into an offscreen texture, mipmapped, and a
panel samples that texture at a coarse level under itself. **A mip chain is a
box blur the GPU already built**, so this costs a texture fetch rather than a
blur pass — which matters on integrated graphics that also has a PS2 to run.

Two rules learned immediately, both the hard way:

1. **Glass does not nest.** Every glass surface samples the same captured scene,
   so a key drawn as its own glass re-samples the bright cover art and ignores
   the darkening of the panel it sits on. It looked like stained glass. **One
   glass layer per modal**; everything on it is an ordinary surface.
2. **The tint must be dark.** A white tint over a blur lightens, and this is a
   dark interface. What makes a material read as a material here is that it
   *dims* what is behind it as well as softening it.

#### The safe area, and whether any of this is really ten-foot

Asked directly, and checked rather than asserted.

**What holds up.** The 1920x1080 canvas, verified identical at 4K, 1080p and
720p. Type from the ten-foot ramp throughout — keys at Title 3, labels at
Callout, the screen title at Title 2. Focus unmistakable at a glance. Every
control reachable by direction plus confirm.

**What did not, and was fixed on the spot.** The keyboard's button legend was
**Caption 1, 25pt**. That size is inside Apple's ten-foot ramp, but it is the
size for something *glanceable* — and a legend telling you what the buttons do
is a line you have to **read**. Now Callout. The rule worth keeping: **anything
a person must read sits at Callout or above; Caption is for things they merely
glance at.**

**What still is not enforced.** The safe area was the last of the six things
tvOS gave Cabinet for free, and it was being met **by inheritance rather than by
design** — the design system's `contentInset` of 60 happens to equal tvOS's own
safe area, because it was copied from there. It is now a named constant
(`kSafeInset`) with a `--safe-area` overlay that draws it, plus a 5% overscan
allowance, so it can be checked on a television rather than reasoned about.

Measured on the running frame:

| | |
|---|---|
| Content bounding box | left **63pt**, right 297pt, top 171pt, bottom 141pt |
| Inside the 60pt safe area | **yes** |
| Inside a 5% overscan allowance (96pt) | **no** — the shelf's own content inset is 60 |

That failure is expected rather than alarming: **60pt is Apple's judgement of
what survives on the televisions people actually own**, and 5% is the analog-era
worst case. Modern sets mostly present 1:1 over HDMI. But it is exactly the
question a monitor cannot answer, and this document already records the hero
being resized three times over it — including once where the simulator showed it
fitting and real hardware did not.

**So: run with `--safe-area` on the SER5, on a real television, before trusting
any of it.**

#### The letterbox glow, built 2026-09-13

Every number taken from `BiasGlow.swift` rather than invented, including one
that would have been missed.

- **The ramp is `peak * (1 - t^1.7)`, not linear.** The shallow exponent gives a
  flat start so the brightness carries toward the physical edge instead of
  collapsing early. **Verified on the rendered frame**: flat for the first ~60
  points, then rolling off to nothing at the screen edge.
- **White, deliberately.** It adds luminance without hue, so it can never clash
  with whatever the game is rendering.
- **The side bars span the full height; top and bottom only the picture's
  width**, so the corners belong to the sides.
- **A static noise dither is composited in**, masked by the same ramp. This is
  the one that would have been missed: **a long near-black ramp bands visibly on
  an 8-bit panel**, and the dither is what stops it. Static, never animated — a
  moving dither in a dark room is a shimmer, which is worse than the banding.
- **It never covers a game pixel.** The shader discards inside the picture rect.
  **Verified**: the darkest pixel inside the picture measures 0.0 with the glow
  at full strength.

**And the background behind a running game is now BLACK**, not the menu's
backdrop gradient. The reference implementation's player clears to black and it
is right for two reasons: a gradient around a game picture is decoration
competing with the thing being looked at, and bias lighting means light against
black — on a purple backdrop it is neither. Caught only by rendering the glow
and seeing it sit on the wrong thing.

Measured on black, outward from the picture edge, at Strong: 12, 12, 12, 11, 8,
4, 0. Subtle by design — this is bias lighting, not an effect.

**Still to judge on a television.** The two peak values, 0.025 and 0.04, were
found with a slider on a real panel after two sets guessed from a mockup were
both wrong. Nothing about a software-rendered VM can confirm them.

**Not there yet:** shaders, and the remaining screens.

**Known gap worth recording now:** there is no text *shaping*, only advance and
kerning from FreeType. That is correct for Latin and adequate for CJK, and wrong
for Arabic, Hebrew and the Indic scripts, which need HarfBuzz. No game library
seen so far needs it; if one does, HarfBuzz is already in the image as a
FreeType dependency.

The real frontend, built against the Phase 0 spec, running on fake data. Home,
browse, game detail, settings, in-game overlay. Full controller navigation, plus
the on-screen keyboard, which everything else that needs text entry depends on.

**Start with the Qt spike** in *The frontend toolkit*, not with a screen. It is
the only toolkit question left worth an hour, and it is cheap. Then:

1. **The focus engine before any screen**, because every screen depends on it
   and it is the thing tvOS gave Cabinet free. Three treatments, remembered
   focus per container, spatial navigation.
2. **One shelf of covers over a black background**, to get the 180 ms focus
   tempo, the 1.10 lift and the caption slide right. Everything else is that
   component in different arrangements.
3. **The on-screen keyboard early**, not last. First-run setup cannot be reached
   without it, and it is the gate on Phase 4 being testable at all.

**Nearly all of this can be built in a VM**, via cage — see *Measured
behaviour*. Layout, colour, typography, artwork grids, navigation, focus, every
settings screen, first-run setup. Controllers too, passed through over USB.

**Except motion.** Software rendering is choppy, so an animation that feels
wrong in the VM may be fine on hardware — and worse, an animation *tuned* in the
VM is tuned against the wrong feedback. Cabinet's design language is
substantially about how things move. Build the motion, do not judge it there.

*Done when* it looks and feels like Cabinet, and every screen can be reached and
left with a controller alone. The "feels" half is answerable only on the SER5.

### Phase 4 — RomM integration
**Status: not started.**

Device pairing and auth, library sync, artwork and metadata, on-demand
downloads with a queue, firmware and BIOS retrieval, save and save state sync.

Storage management distinguishes **cached** from **kept** (see *Emulation*):
cached games are evictable, kept games are not, and the user moves games between
the two.

**First run copies Cabinet's flow rather than inventing one.** Read from the
source 2026-09-13: tvOS already solves this problem and has shipped the answer.

1. **`ServerSetupView`** — one field, the RomM address, and nothing else. Its
   own note explains the copy: *"the one thing someone genuinely wonders here:
   no password is coming."*
2. **`PairingView`** — the app asks the server to start a device authorisation,
   shows a short code, and **displays the approval URL as a QR code**, because
   *"tvOS has no comfortable way to type into a browser with a remote."* The
   person scans it with the phone already signed in to RomM and approves there.
   The app never sees the password, and the token can be revoked server-side.

**So the whole typing burden on a television today is one hostname.** No
password, ever. That number matters, because it is the size of the problem any
first-run convenience is competing against.

#### Getting a ROM out of RomM, measured

**Tested against the live server 2026-09-14** by downloading one, because the
path from a library entry to a running game had never been walked.

**ROMs arrive zipped.** `fs_name` is `Tetris.zip`; the archive holds
`Tetris (World) (Rev 1).gb`. Nothing hands a libretro core a zip, so the
frontend unzips. Endpoints:

| | |
|---|---|
| `GET /api/roms/{id}/files` | the files a ROM is made of — multi-disc, or a `cue` beside its `bin` |
| `GET /api/roms/{id}/content/{file_name}` | the bytes |

**And the cores want opposite things, which decides the design:**

| Core | `need_fullpath` | Wants |
|---|---|---|
| gambatte | no | the ROM as a **buffer in memory** |
| genesis_plus_gx | **yes** | a **real path on disk** |

`retro_get_system_info` reports this per core, so **ask the core, never the
platform**. The launch path therefore needs both halves — unzip to memory for a
buffer core, unzip to a file for a fullpath core — and a fullpath core with a
multi-file game needs every file beside it, not just the one that was asked for.

**What is NOT yet proven**, and was wrongly implied before this was checked:
Dr. Mario ran from a **hand-placed local file** with `--core` and `--rom` as
command-line paths. The core host loading a `.so` and a ROM *from disk* and
running it with save states is real. Everything between RomM and that point —
downloading, unzipping, where a downloaded ROM lives, what evicts it, choosing
the core from the platform, and reaching any of it from the UI — does not exist.

#### How saves, states and firmware actually work, read from Cabinet

**Read 2026-09-14 from `RommApp/RommApp/Auth/RommClient.swift`,
`Native/NativeLauncher.swift`, `Native/NativeCore.swift` and
`Native/KeptGames.swift`.** This was being reasoned about from the API surface,
which was the wrong way round: tvOS and macOS already ship the answer.

**The console is not the home for any of this. RomM is.** Saves, save states and
memory cards all live on the server and all come back down. The reference server
holds 80 saves and 54 states already, several of them written by Cabinet.

##### Saves and states are different endpoints, on purpose

| | |
|---|---|
| Battery saves, memory cards | `POST /api/saves?rom_id=&emulator=&overwrite=true`, multipart, part `saveFile` |
| Save states | `POST /api/states?rom_id=&emulator=`, multipart, part `stateFile`, optional `screenshotFile` |
| Reading either | `GET /api/saves?rom_id=` / `/api/states?rom_id=`, then `/{id}/content` |

**`overwrite=true` on saves is load-bearing**: it replaces the server's copy of
the same file name instead of stacking a row per upload, which is *"what keeps a
PS1 game at one memory card rather than one per session."* States deliberately
do not overwrite — a state history is the point of states.

##### The `emulator` tag is the compatibility mechanism, and CabinetOS must get it right

Every upload carries an `emulator` string. Cabinet's is per **core**, not per
platform — `gambatte-native`, `gpgx-native`, `pcsx-rearmed-native` — and its own
comment says why:

> *libretro state formats are core-build-specific, so each player's states are
> tagged distinctly on purpose: a separate tag keeps each player's launch UI
> from offering states it cannot actually restore.*

So the tag is what stops a launch screen offering a state that will fail. Which
raises the decision CabinetOS cannot avoid:

> **Does CabinetOS write `gambatte-native`, or a tag of its own?**

**It should write the same tag — and only because of work already done.** The
whole point of the Phase 3 result is that a Gambatte state is bit-identical
between Cabinet's macOS arm64 build and a Linux x86-64 build *at the same
commit*. `core-manifest.json` pins that commit, `build-core.sh` asserts it, and
CI proves the artifact is reproducible. Those three together are what make
sharing a tag safe rather than optimistic.

**The rule, therefore: share Cabinet's tag only where the build is provably the
same thing** — same pinned commit *and* the same build arguments. Where
CabinetOS pulls a different lever, it must use a different tag, or Cabinet's UI
will offer the person a state that cannot load. Today gambatte and
genesis_plus_gx both match Cabinet's configuration, so both share. The
recompiler-sensitive cores are exactly the ones where this has not been settled,
which is one more reason open question 13's remaining half matters.

A tag that is wrong in the safe direction costs a greyed-out state. Wrong in the
other direction, it costs someone their progress.

##### Firmware: fetch everything the platform lists

From `NativeLauncher`:

> *Fetches every firmware file the platform lists rather than assuming which one
> the board wants: a core looks BIOS files up by name in the system directory,
> ignores what it does not need (Beetle Saturn wants one of two region BIOSes;
> FBNeo boards like CV1000 need none at all), so extra files are harmless and
> missing ones are the only failure that matters.*

So: `GET /api/firmware?platform_id=`, download all of it into the system
directory, and let the core pick. Do not try to be clever about which BIOS a
given game needs.

###### Firmware is per PLATFORM, so fetch the whole lot once and stop thinking about it

**MMagTech, 2026-09-16: a BIOS "just needs downloading for that platform one time
and then the platform uses it for all games on it".** That is already how
CabinetOS stores it and it is better than the reference implementation here —
one shared `system/` directory, with a file already present at the right size
skipped. Cabinet's tvOS stages firmware into each game's own cache directory
instead, so twenty-seven Dreamcast games mean twenty-seven copies of
`dc_boot.bin`.

**What is still per-launch is the asking**, and it need not be. Every launch
calls `fetchFirmware(platformId)` before the ROM, even when every file is
already on disk: a round trip each time, and offline it fails and logs a
complaint on a launch that was going to work anyway.

**Measured against the live server, 2026-09-16, which settles it:**

| | |
|---|---|
| All firmware, every platform | **212 MB** |
| PlayStation 3 alone | **197 MB** |
| **Every platform this console has a core for** | **~15 MB** |

Ninety-three percent of that total is firmware for a system with no core in the
manifest and no prospect of one. For everything actually playable it is fifteen
megabytes — the entire BIOS collection, for every system, permanently.

**Fetching the whole 15 MB at setup was proposed and MMagTech chose otherwise:
fetch a platform's firmware the first time a game on that platform is launched.**
He is right, on two counts. It is less machinery — the launch path already does
exactly this, and the only change is not asking again afterwards — and it is a
simpler thing to hold in your head: the console fetches what a game needs when
that game needs it, with no separate preparation step. Fifteen megabytes is not
enough saving to justify inventing a setup phase for.

> **Fetch a platform's firmware on the first launch of a game on it, and never
> ask again. Keeping a game fetches its platform's firmware too.**

**The second sentence is the one that is easy to forget**, and it is Cabinet's
behaviour already: *"Keeping a game pulls its ROM and its platform's
firmware."* Without it there is a real hole — download a PlayStation game for
later, go offline, and it will not start, because the machine has the game and
not the system file it needs. Keeping is a promise that a game will work later,
and later may have no network in it.

**Checking is not downloading, and the two should not be confused.** The console
can ask what firmware a platform *has* without fetching any of it, which is a
cheap list request and is all the missing-BIOS warning below needs. So: ask
early, download when first needed.

Two details worth keeping: `missing_from_fs` files are skipped, since the server
lists them and does not have them; and a failure is still not fatal, because
which BIOS a core needs is the core's business and most platforms need none.

**Fetch again when a platform appears that was not there at setup.** A library
grows; someone adds Saturn games next month. "Once" means once per platform, not
once per console.

###### Tell the person their server has no BIOS for a system, BEFORE they pick a game

**MMagTech, 2026-09-16, and it is a real gap.** Today a platform whose BIOS the
server does not hold fails at launch with the CORE's error message — measured
earlier the same day, on Sega CD: `Unable to open CD BIOS:
"system/bios_CD_U.bin"`. Clear, actionable, and delivered at the worst possible
moment, after the person chose a game and waited for a download.

**Asking what the server holds is what makes the better version possible**, and
it costs nothing: a firmware list per platform, no downloads. Do that while the
library is being scanned and the console knows, up front, which systems it
cannot play — so it can say so once, about a whole system, rather than per game
and after the fact.

It needs one small thing that does not exist: **a list of which platforms cannot
start without firmware at all.** PlayStation, Saturn, Sega CD, 3DO, Dreamcast,
PS2 and TurboGrafx-CD; most systems need nothing. That list is stable, short,
and a property of the hardware rather than of anyone's library.

**The honest limit, and it is why this is a coarse check rather than a precise
one:** *which* BIOS a given game wants is genuinely not knowable up front — this
section already records Beetle Saturn taking either of two region BIOSes and
FBNeo boards needing none — so the console must not try to verify that a
platform's firmware is *sufficient*. What it can say with certainty is that the
server offered **nothing** for a system that cannot boot without something, and
that is the case worth warning about.

Where it belongs is `catalog`, beside the four answers it already gives for why
a game cannot be played. This is a fifth, and unlike the others it is a fact
about the person's server rather than about this console or the manifest.

##### What a kept game is

`KeptGame` embeds **the whole `Rom` captured at keep time**, not a subset, so a
kept game can be browsed and launched with no network at all — cover paths and
platform identifiers included. Its directory holds the ROM *and* its firmware,
so it boots with zero requests. States stay internal to it and are deliberately
never exposed in the Files mirror, because they are core-build-specific and
belong to RomM's database rather than its filesystem layout.

##### What this means for eviction

Almost nothing on the console is irreplaceable, which makes reclaiming space
safe: ROMs, firmware, saves, states and memory cards all come back from RomM.
**The one thing that cannot be re-fetched is something written locally that has
not been uploaded yet** — hence "local first, always", and a `pending-states`
directory that eviction must never touch.

It also adds a requirement this project had not accounted for: **the console
must upload, not merely download.** That needs write scope on assets, which the
first pairing did not request.

#### When a save happens — decided 2026-09-15

Keys currently trigger it: F5 writes a state, F8 restores the newest loadable
one, F6 pushes the game's own save. **That is the test environment and not the
product.** The intended triggers, settled rather than deferred:

- **When the game writes its memory card**, so a save that the game itself
  considers made is a save the console has.
- **From the in-game menu**, as a deliberate act.
- **On leaving a game**, always — nobody should lose progress because they
  quit.
- **A controller combination**, so a state can be taken without opening
  anything.

Cabinet's shape for the same thing: *"the views own the* when*, the sync engine
owns the* what*"* — the triggers belong to the screens, the capture and upload
belong to one shared type. CabinetOS should keep that split from the start,
because tvOS once carried its own copy of the *what* and it silently went stale.

**Nothing that talks to a server may stop the picture. Fixed 2026-09-15.**
Uploads were on the frame thread, which on a LAN with a 60 KB state was
imperceptible and on a slow link is a visible hang — and against a server that
does not answer, curl's timeout would leave the console looking dead for thirty
seconds.

The split that matters is not "put it on a thread", it is **which** part moves:

- **The frame thread reads the core and writes the local copy.** Reading has to
  happen there because a core is not thread-safe, and the local write has to
  happen before the upload is queued, or *local first* stops being true the
  moment the process dies between the two.
- **Only the network moves to the worker.** That is the part that can take
  thirty seconds.

**Measured, saving a state and a memory card together: 3.21 ms on the frame
thread**, which is a core read and two local writes, inside a single 16.7 ms
frame. Every upload completed afterwards on the worker.

Loading a state is network work too and got the same treatment: the search and
fetch happen on a worker, and only applying the state touches the core, which
waits for the frame thread.

**The queue is drained on the way out, not abandoned.** Anything still pending
is a save someone has already made. Quitting is the one place waiting for the
network is correct, because there is no picture left to stop.

#### Downloads stream, and the console keeps drawing

**Built and measured 2026-09-14.** Two things were wrong with the first working
launch, and only one of them was the obvious one.

**It held the whole ROM in memory.** Fine for a 19 KB Game Boy file; the same
library holds a 1.78 GB arcade set, and a 4 GB console does not get to keep one
of those in a `std::vector`. `Client::fetchToFile` streams to disk through a
buffer and never holds the body.

**And it downloaded on the frame thread**, which is the worse of the two because
it is the half a person experiences: press a button on a large game and the
console freezes solid — no animation, no progress, no way to change your mind —
for as long as a few gigabytes takes. A worker does it now; the frame loop reads
an atomic snapshot and keeps drawing. `romm::Client` was deliberately built
synchronous so callers could do this, the same way `ImageCache` already did.

Details that are not incidental:

- **Written to `<name>.part` and renamed only on success**, so an interrupted
  download can never be mistaken later for a complete ROM.
- **No whole-transfer timeout.** A deadline is wrong for a file that can
  legitimately take twenty minutes on a slow link; a *stall* is caught by a
  low-speed limit instead.
- **Escape cancels the download rather than quitting.** Three gigabytes is a
  long time to be unable to change your mind. The progress callback returning
  false is what reaches a transfer already in flight.
- **A progress bar only when the server declared a size.** It often does not,
  and a bar that invents its own total is a lie — the honest fallback is to show
  what has arrived and draw no bar.
- **An archive is untrusted input.** A member named `../../etc/thing` is reduced
  to its last path component, so it lands inside the cache directory or nowhere.

**Measured, streaming a 112 MB Sega CD image:** the file arrived complete, the
`.chd` was correctly passed through rather than unpacked, Genesis Plus GX loaded
it, and the refusal was `Unable to open CD BIOS: "system/bios_CD_U.bin"` — a
clear, actionable message rather than a silent failure. **Sega CD needs firmware
from RomM**, which is a scope this console's token does not currently hold.

**What this still does not do.** A ROM already on disk at the size RomM reported
is reused rather than re-fetched, which is the crude half of *cached versus
kept*. **Nothing evicts anything.** A library of 1644 games at these sizes will
not fit on a console, so the disk fills and stays full. That is the next thing
this needs.

#### The cache policy — decided 2026-09-16, and the core of it now runs

**The rule is that the person never thinks about storage, and never loses
anything they would miss.** Everything below serves those two sentences. From
MMagTech's proposal, with four changes argued for rather than accepted.

##### What Cabinet already does, on both its platforms

**Read from `NativeLauncher.swift` 2026-09-16 at MMagTech's prompt, and it should
have been read before any of this was designed.** Neither Apple platform has an
eviction policy, for two different reasons, and the difference is the whole
reason CabinetOS needs one.

**The Mac has no cache at all.** A game is either *kept* — chosen by the person,
in a permanent directory, never touched — or it is downloaded into a temporary
directory that is deleted when the player closes. `cleanUpTempDirectories()`
runs on the way into a launch as well as out of one, "so temp space holds at
most the one game about to load". Two states, no middle, nothing to decide.

**The Apple TV has a cache and delegates the deleting.** It writes into the
system caches directory keyed by rom id and lets tvOS reclaim it whenever it
likes, system-wide across every app — and when the file has gone, the next
launch simply downloads again with, in its own words, "no special handling
needed".

> **CabinetOS is the only one of the three that has to decide for itself, and
> that is not an oversight in the design — it is what being the operating system
> costs.** There is no prior art to copy here because neither sibling has the
> problem.

**Two things do carry over.** The shape is the same one already specified in
*Emulation*: kept versus transient, with keeping being the deliberate act. And
the Mac is proof that **"delete it when they stop playing" is shippable** — it
is what that app does today — so discarding is the safe fallback wherever any of
the machinery below is uncertain, rather than something to be nervous about.

**And one warning, from tvOS's own history.** It used to behave exactly like the
Mac, and that is recorded as a mistake: every launch "used to redownload into a
fresh temp directory deleted unconditionally on exit, so replaying a game
already on Recent or Favorites cost a full download every single time even
though nothing about the file had changed." Replaying the same handful of games
is the living-room pattern, and it is CabinetOS's pattern too. **So the middle
tier has to exist here, even though the Mac gets away without one.**

##### None of this may be tuned to one library, one disk or one connection

**Raised by MMagTech against the first draft of this section, and he was right.**
That draft justified its eviction order with "every cartridge game in the
library together is under 2 GB", which is a fact about *this* reference library
— about three hundred cartridge games — and it inverts for anyone with a
complete set, where the cartridge half is tens of gigabytes. It then closed by
saying the threshold should be settled against a real library, which bakes in
whichever library happened to get measured.

*Hardware* already has this rule in this document: the SER5 "sets the
performance floor, not the ceiling", and CabinetOS "must not have quietly grown
dependencies on this particular box". **The library is the same kind of
reference and deserves the same sentence.** The first draft did not give it one.

So the standard for every rule below is that it holds for all four corners, and
the reference library is an illustration in the margin rather than the basis:

| | |
|---|---|
| **A library of one shape** | all cartridges, or all discs, or a mix |
| **A disk of any size** | a 32 GB eMMC stick and a 4 TB NVMe |
| **A library far larger than the disk, or far smaller** | permanent pressure, or none ever |
| **A connection of any speed** | see below — this is the assumption that matters most |

**Where a number is unavoidable, express it as a fraction of something the
machine can measure**, not as a constant somebody chose while looking at their
own collection.

##### The assumption underneath all of it: that a re-download is cheap

**Stated because the first draft relied on it silently.** The argument that
eviction is harmless — "it is still on the server and comes back in minutes" —
is true against a RomM on the same fast LAN, which is this project's own setup.
It is false for a server in another building, over WiFi, or across the internet,
where a 4 GB game is twenty minutes rather than forty seconds.

**That does not change what is safe to delete. It changes whether deleting
quietly is the right manners.** Where re-fetching is cheap, handling it silently
is the console-like behaviour this whole section argues for. Where it is
expensive, the same silence spends twenty minutes of somebody's evening.

**DECIDED: the behaviour does not change, because the answer already exists and
it is `keep`.** Someone on a slow link marks the games they care about, and the
console never touches those — that is precisely what keeping is for, and it is
better than the alternatives on every count:

- **Measuring throughput and switching behaviour** means the console acts
  differently on Tuesday than it did on Monday, for reasons invisible to the
  person using it. A console that is unpredictable is worse than one that is
  occasionally slow.
- **Asking before each eviction** is a dialog box about storage, which is the
  exact thing *What CabinetOS is* rules out, and it would fire most often for
  the person least able to act on it.

So the policy below is the only behaviour, not a fast-link default. What a slow
link changes is the *advice*: first-run and the Storage screen should say that
keeping a game means never waiting for it again, which is a sentence worth
writing regardless.

##### BUILT AND MEASURED, 2026-09-16

**The disk no longer fills and stay full**, which is what this whole section was
for. `frontend/src/cache.{h,cpp}` holds the eviction, and the download path asks
it for room at the two moments described below.

Proved by filling the test machine's disk rather than by reasoning:

| | |
|---|---|
| Free space squeezed to | 120 MB |
| Game asked for | Twisted Metal, 178 MB, needing 187 MB with overhead |
| Evicted | `Mad Dog McCree.chd`, dated 09-10 — **the oldest, and only it** |
| Left alone | `Colin McRae Rally.chd`, dated 09-14, four save states, an `.srm`, a memory card |
| Result | downloaded, launched, **34,594 frames of PlayStation** |

It stopped the moment there was room rather than clearing everything it could,
which is the margin rule working, and nothing irreplaceable was a candidate at
all.

**What is NOT built**, so that nobody reads the above as more than it is: there
is no keep, so nothing is protected as kept; nothing tracks pending uploads, so
that protection is a comment rather than a check; and the system reserve for
updates is unimplemented. Eviction today protects the running game and nothing
else, because nothing else exists yet to protect.

###### The bug that only running it could find: deleting a file frees nothing

**CabinetOS runs on btrfs, where `unlink` returns immediately and the space
stays invisible to `statvfs` until a transaction commits.** So the first version
deleted exactly the right file, measured again, saw no change, and reported that
there was nothing left to clear.

Measured on the machine rather than guessed from the symptom:

| | |
|---|---|
| Before deleting a 50 MB file | 218,812,416 free |
| Immediately after | 218,812,416 — **no change** |
| After three seconds | 218,812,416 — **still no change** |
| After forcing a commit | 268,816,384 |

Waiting is not a fix, because it is not a race. `evictUntilFree` now calls
`syncfs` on the cache's own filesystem once, after deleting.

**It is not a btrfs workaround to be removed later.** On ext4 or xfs the space
is already accounted and the call returns almost immediately, so the code is
correct everywhere without knowing where it is. Which matters, because **the
filesystem on real hardware is not established** — the VM is btrfs and Bazzite's
lineage defaults to it, but the installer ISO has never been booted.

**And it is the second time in one day that a thing passed every check and was
still wrong until somebody ran it**, after melonDS's save directory. Both were
invisible to the build and obvious within one launch.

##### The policy in one paragraph

**MMagTech's, 2026-09-16, and it is better than the version it replaced because it
is sayable.** Everything else in this section is detail underneath it:

> **The games you have played on this console are on the disk. They stay until
> the disk needs the room, and then the ones you have not played for longest go
> first. Nothing that is running, nothing you marked as keep, and nothing still
> waiting to reach RomM is ever touched.**

**The cache is invisible, and that is the decision.** MMagTech, 2026-09-16,
ending a long detour: *"No one knows or cares if the game exists in cache on the
OS. You go to the game and hit play. If it isn't cached it downloads. If it is
cached it doesn't."*

That is right, and the reason it is right is that **the feedback already exists
at the only moment it is useful**. Pressing play on a game that is not on disk
already shows a progress bar and already takes Escape to back out. So the person
finds out immediately, at the point of asking, and can change their mind for the
cost of one button press.

**A badge warning them beforehand does not shorten the download.** They want to
play that game; the information changes nothing they would do, and it adds a
thing to think about to a screen whose whole job is that there is nothing to
think about.

So: no promise about what is cached, no marker on the shelf, nothing in the UI
at all. **Everything below this line is internal.**

The one exception is the Storage screen, which stays — because it is somewhere a
person goes *deliberately*, looking for exactly this. Nobody cares until they go
looking, and then they should find it.

**Two things this deletes**, both recorded so nobody re-adds them:

- **The downloaded badge on shelf cards**, and with it the whole question of what
  the shelf promises about the disk.
- **"Cleared 40 games" as a worry**, which was the main argument for the deferred
  size rule below. It was a concern about how a list would read, on a screen
  nobody is watching.

**And "longest since played" means since PLAYED, not since downloaded.** A game
fetched months ago and played last night stays; a game fetched last night and
never started goes first. Easy to implement backwards.

##### Never on a timer. Only under pressure, only at a safe moment

**Nothing is evicted because time has passed.** A cached game on a half-empty
disk costs nothing and deleting it only buys a re-download. Expiry is the
intuitive answer and it is the wrong one.

**The safe moment is the start of a download that will not fit.** Free exactly
enough for the incoming game, least-recently-played first, and stop the moment
there is room. Nothing is deleted speculatively, in the background, or while a
game is running.

**Pressure is simply the disk being full**, and there is no cache size to
configure. MMagTech, 2026-09-16: a person picks a game and chooses Download, those
downloads stay, "and by nature shrink disk space available for cache".

That is the whole sizing rule, and it deletes a setting:

> **The cache is whatever is left AFTER the system's own needs.** Kept games
> take what they take, the save floor and the system reserve are never crossed,
> and the cache has the remainder.

An earlier draft had a configured budget *and* a free-space limit, whichever
bound first — two numbers doing one number's job, and the configured one is
unanswerable anyway. Nobody knows what to set a cache size to, and on a console
the drive is for games regardless.

##### The games and the operating system share one disk

**MMagTech, 2026-09-16, and it is the most serious thing raised about this
policy.** `/` is a 43 MB read-only composefs and **all real storage is `/var`** —
which holds the ROM cache, the OS's own storage, and the space a system update
needs to stage itself. They are not separate.

So a disk full of games is a console that **cannot update itself**, and this
document's entire update model is a new image pulled and rebooted into. An
in-place upgrade already measured at 1.0 GB of changed layers against a 5.0 GB
image; a base bump moves considerably more.

> **The cache is always available to the system, taken silently, without
> asking.** Nobody should ever see "may I delete a game so I can install an
> update?" — there is no basis on which to answer it and the answer is always
> yes. Deleting a cached game costs a re-download; a console that cannot take
> its own security updates costs rather more.

**This is why the cache cannot simply be "everything left over", which an
earlier draft of this section said.** The danger is not the cache, which is
disposable by definition and can always be taken. It is **kept** games, which
are the one thing this policy refuses to delete: keep enough of them and an
update becomes impossible, permanently, with nothing the console is allowed to
do about it.

**So the system reserve is enforced in exactly one place — at the moment of
keeping.** Keeping is already the one action the console may refuse, for the
save floor; this is the second and larger reason. The cache itself needs no
protecting from the system, because it is the system's to take.

##### Partitioning was considered and rejected

**Raised by MMagTech in the same breath, and reasoned to the right answer: a
separate system partition would enforce this in the kernel rather than in our
code, and the problem is that nobody can say how big it should be.**

Three reasons it stays one filesystem, beyond the sizing guess:

1. **A partition is a wall in BOTH directions.** As things are, an update that
   turns out larger than expected eats into the game cache and still works. With
   a system partition, an OS side that fills — a bigger update, logs, anything
   unforeseen — cannot touch the two hundred gigabytes sitting free on the games
   side. One filesystem is the more forgiving arrangement, not the riskier one.
2. **Wrong is recoverable on one side and not the other.** A reserve that turns
   out too small is a number changed in the next update. A partition that turns
   out too small is a reflash.
3. **It is not this project's layer.** Constraint 1 is to stay at the
   application layer; the disk layout belongs to the base image, and
   `disk_config/disk.toml` currently declares a single filesystem at `/`.

**The honest argument FOR a partition**, which is why this is recorded rather
than dismissed: it does not depend on our code being correct. A bug in the
reserve logic fills the disk; a partition could not be filled by games whatever
we got wrong. That is real — and the failure it prevents is recoverable (delete
games, or let the system take the cache) while the failure it introduces is not.

**And the proper separation already exists in the design, one level up.** Open
question 14 gives the person a choice of where games live — a second SSD, a USB
drive. Take that and the system disk is untouched by games and none of this
applies. **That is a real separation, chosen by somebody who knows their own
hardware**, rather than a number this project picks at install time for a machine
it has never seen.

So the reserve is the answer for the single-drive case, which is the common one
rather than the only one.

**A number, and it is the least certain thing here:** enough for a full image
rather than a typical delta, since the case that matters is the one where the
cache is already empty and only kept games remain. That is the far side of 5 GB
and wants checking against a real update on real hardware rather than guessing
here.

**It degrades exactly the right way.** Keep enough games and the cache shrinks
to nothing, at which point every un-kept game downloads, plays, and is dropped
on the way out — which is precisely what Cabinet's Mac does today, and it ships.
Keep so many that nothing fits at all and the download refuses and says so,
which is the one failure this policy ever shows anybody.

##### Everything on the disk is a copy of RomM. That is the whole rule

**MMagTech, 2026-09-16, after this section had drifted into categories for the
third time: saves, BIOS and memory cards are all stored on RomM.** They are, and
this document has said so twice and then built tiers of protected things on top
of it anyway.

> **Everything here is a copy of something on the server. The only exception is
> what has not been uploaded yet.**

So **everything is evictable** — ROMs, save states, battery saves, memory cards,
firmware. There is no protected tier, because there is nothing to protect. The
earlier draft's "counted but never evicted" list was inventing a distinction the
server had already removed.

**Saves and firmware simply never come up**, which is an observation rather than
a rule. Measured on the test machine: 124 MB of ROMs against 40 KB of battery
saves and 384 KB of firmware. Every save for all 1644 games in the reference
library is around 33 MB. They will never be the largest thing in a list sorted
by size, so nothing needs to say they are special.

**And the previous draft's reason for exempting firmware was simply wrong** —
"deleting it breaks the next launch of that system". It does not. It re-fetches,
like everything else does.

##### Save states can be the biggest thing on the machine, not a rounding error

**Corrected 2026-09-16, and the first estimate here was badly wrong.** It assumed
states are taken at checkpoints and put fifty of them at 325 MB.

**People save-scum.** Grinding through a hard section means a state every twenty
seconds or so, which is around 360 in a two-hour evening. At the **6.5 MB** a DS
state measures — a real number from this project's own melonDS build — that is
**2.3 GB from one session**, more than most ROMs on the disk. PS2 will be larger
still. States do not overwrite, deliberately, because the history is the point.

So the conclusion inverts. States are not a small thing to be exempted, they are
one of the largest things to be managed, and they are on RomM with their
screenshots like everything else.

> **Keep the newest state for a game as long as its ROM is there** — it is the
> one that gets loaded — **and let older states be ordinary candidates**, fetched
> back from RomM when somebody actually picks one off the launch screen.

That is the same principle as the ROM cache, applied one level down, and it
needs no new machinery: they join the same oldest-first list.

##### The case this does not solve: save-scumming while offline

**The upload queue is the one thing that cannot be evicted, and save-scumming is
exactly what makes it enormous.** An evening of it with no server reachable is
gigabytes of pending states by morning, and no floor protects against data that
is itself the thing filling the disk.

This was already recorded as an open problem in a milder form — "a long spell
offline defeats the floor" — and the realistic magnitude makes it worth solving
rather than noting. It is a conversation with the person ("this console has not
reached your server in three days") rather than a storage rule, and **it belongs
with whatever handles being offline, not here.**

A cheaper half-answer exists and is not chosen: while offline and short of
space, the oldest *pending* states for a game could be dropped rather than the
newest, since a save-scummer wants the last one and not the three hundredth from
the bottom. That trades a promise this document makes — local first, nothing
written is ever lost — against a disk that stops working, and **that trade needs
MMagTech rather than an assistant.**

##### The eviction unit is a FILE, not a game

**This is the first change to the proposal, and it is structural.** Today a
game's cache directory holds the ROM *and* its save states *and* its battery
save together — `romcache/2813/` has three `.state` files and an `.srm` beside a
1 MB Game Boy ROM. So "evict a game" would delete the one thing that can always
be fetched again along with the only things that cannot.

The proposal patches this with a rule — never evict a game with unsynced saves.
That is correct and it should not be necessary. **Separate what the console
wrote from what it downloaded**, and the ROM becomes unconditionally safe to
delete rather than conditionally, because there is no longer anything precious
in the same unit to take with it. The case where a download fails over a few
kilobytes of old save goes away with it, which is a poor trade to have been
making.

What the rule protects is then the **upload queue**, not save data in general —
a distinction that matters, because a save already on RomM is a cache like the
ROM beside it.

What remains protected, and it is now short:

| | |
|---|---|
| The running game's ROM | it is in use |
| The running game's save data | it is being written |
| **Anything not yet uploaded** | the only irreplaceable data on the machine |
| Anything **kept** | the person asked for it; *Emulation* already says this is never automatic |

**And what is evictable is wider than ROMs, for the same reason.** A synced save
state is a cache of RomM like everything else, and a well-played DS game can
hold hundreds of megabytes of state history that would cost a few megabytes to
fetch back on demand. It belongs in the same candidate list, ordered the same
way — no separate mechanism, and the size rule below already keeps it out of the
way when it is not worth taking.

The unit is therefore **any local file that RomM can return**, which is a longer
sentence than "the ROM" and the same idea.

##### The floor has to be enforced DURING the download, not before it

**Second change.** The failure the reserve exists to prevent is a save that
cannot write because a download filled the disk — and that happens *while* the
download runs. Checking once at the start does not prevent it, and the size is
not always known: this document already records that the server frequently
declares no length, which is why there is a progress bar only sometimes.

So the streaming writer checks free space as it goes and aborts when the next
write would cross the floor, deleting its `.part` — which returns the space it
had taken. A `statvfs` every few megabytes is not a cost worth optimising.

**The size of the DOWNLOAD is in the library record, not the HTTP response.**
`fs_size_bytes` is present on every ROM and is what makes room for the transfer
possible to reserve at all. Do not reach for `Content-Length`.

It is the size of the *archive*, though, so it is what the download needs and
**not** what the game will cost once unpacked. See below.

##### Unpacking: ask the archive, do not guess a ratio

**Third change. The first draft said "budget twice the archive" and that is
wrong in the direction that fills the disk**, as MMagTech pointed out: an archive
is *compressed*, so what comes out of it is not the size that went in.

Measured on this library rather than argued: Space Harrier is an **868 KB** zip
holding a **2 MB** ROM. Extraction holds both, so the peak is 2.9 MB — **3.4×
the archive**, not 2×. And that is a mild case. DS and N64 ROMs are padded out
to power-of-two sizes with empty space, which compresses far harder, so no
multiplier is safe for the set.

**There is no need to estimate at all.** A zip declares each entry's
uncompressed size in its own index, 7z likewise, and `archive_entry_size()`
hands it over when the header is read — before a byte is extracted.
`romfile.cpp` already walks those headers to pick the member to use; it simply
does not add up the sizes while it is there.

> **peak = the archive + what its index says will come out of it**, known before
> committing to the extraction, and refused cleanly if it will not fit.

**Which means the space check happens TWICE, at two different moments**, and it
is the kind of thing that gets built as one check and surprises somebody later:

| | | |
|---|---|---|
| **Before the download** | the archive's size, from RomM's `fs_size_bytes`, plus a small percentage | it is all that can be known yet |
| **After it, before unpacking** | what the archive's own index says comes out of it | the ratio is not a percentage and cannot be guessed — 868 KB of Space Harrier becomes 2 MB |

Either check can trigger eviction; the second usually passes. **And it does not
apply at all** to `.chd`, `.rvz` or an arcade set handed over unextracted, which
is most of the large files in a library.

**The small percentage on the first check is not decoration.** Landing on
exactly zero free bytes is where filesystems start failing in interesting ways,
and it costs nothing to stay off it.

Same idiom as everywhere else in this document: ask the core what it takes, ask
the magic bytes what the file is, ask the archive what it holds. The multiplier
was a guess standing in for a fact that was already on disk.

**A format that declares nothing** — some streamed archives report an unknown
entry size — is the only case needing a fallback, and the honest fallback is to
extract into the space available and fail if it runs out, not to invent a ratio.

Two exceptions already established elsewhere, worth restating because they make
the peak vanish entirely where they apply: an arcade set is handed to FBNeo
unextracted, and `.chd` and `.rvz` are never unpacked at all.

##### What a game costs is what is ON DISK, not what RomM said it was

**Falls out of the above and is its own accounting error.** The policy's budget
would naturally use `fs_size_bytes`, since that is what the library record
carries and what "free exactly enough for this game" was written against. For
anything archived that is the *compressed* size, and the cache ends up holding
the larger thing. Budgeting against it under-counts every archived game.

**And today it under-counts twice over, because the archive is never deleted.**
`romcache/39/` holds `Tetris.zip` and the extracted `Tetris.gb` side by side,
and that is not an oversight: the "do we already have this?" check `stat`s the
downloaded archive against the size RomM reported, so deleting it would mean
re-downloading the game on every launch.

So the unpacking cost is not transient at all as things stand — it is permanent,
and it is the compressed size of every archived game in the cache, forever.

> **Decided: the archive goes once it has been unpacked, and the reuse check
> moves to the extracted file.** It costs a small per-game record of what was
> unpacked and how big it should be — which the cache wants regardless, since
> the budget has to be computed from real sizes on disk rather than from the
> server's idea of them.

##### Order: least-recently-played, and that is the whole rule

**Fourth change, and it answers "should size matter" and "should small systems
be exempt" with one mechanism.**

The intent is easy to state: **do not delete a hundred things that were cheap to
keep in order to house one thing that is not.** Freeing 4 GB by removing four
thousand Game Boy games is a bad trade whatever the library looks like — each
one is a separate thing somebody may come back to, and together they were
costing almost nothing.

**The threshold that expresses this has to scale with the need, not with a
number somebody picked.**

> **Shipped rule: least-recently-played, full stop. The size refinement below
> is DEFERRED until there is evidence it is needed.**

The refinement it defers: ignore candidates smaller than one percent of the
space being freed, so that a single 4 GB download does not take forty Game Boy
games with it.

**Its reason has since evaporated.** The argument was that "cleared 40 games"
reads as destructive — a worry about how a list would look, on a screen the
decision above says nobody is watching. What remains is the actual cost, and the
actual cost is that forty small games come back in a second or two each.

**So it stays deferred and it may never be needed.** Kept here because the
arithmetic is done and someone will think of it again.

One percent is recorded rather than left to be re-derived, because it has no
units and belongs to no library. Needing 4 GB it ignores anything under 40 MB,
leaving cartridge games alone. Needing 50 MB for a Game Boy Advance title it
ignores anything under 500 KB, so cartridge games ARE the candidates. **The same
rule gives the opposite answer when the library is the opposite shape**, which a
fixed megabyte threshold could never do.

**Exempting small systems outright was considered and rejected**, though the
instinct behind it is right. A permanently exempt class can grow past the budget,
and then the disk is full of things nothing is allowed to delete — with
*Download All* (which this document says CabinetOS should offer, reversing
tvOS's call) that is not a hypothetical. This version has the same practical
effect and cannot reach that state.

When it is added, it is keyed on **size relative to the need**, never on
system: that needs no table of platforms to go stale, and a system's name was
never the thing that mattered.

##### Free a margin, not exactly enough

**Also changed, and it is the difference between eviction being an event and
eviction being constant.** "Free exactly enough for the incoming game" is the
obvious rule and on a disk that sits near its budget it means evicting on every
single launch, forever, until the cache holds nothing but the game currently
running. The person never sees it, they just never benefit from the cache again.

So free enough for the incoming game **and a margin beyond it**, so that the
next few launches cost nothing. The margin is a fraction of the budget rather
than a size — a tenth — which keeps it sensible on a 32 GB stick and on a 4 TB
drive without being told which one it is on.

##### "Least recently played" means on THIS console

Play history belongs to RomM and this document says the console should keep no
local notion of it. **Eviction order is a different question**: not "when did
this household last play this game" but "when did this machine last use this
copy". A game played on the Apple TV yesterday is not evidence that the copy on
this disk is worth keeping.

So the timestamp is a property of the cache, written when a ROM is launched
from it. Recorded here because it looks like the rule it is not, and because
depending on RomM would make eviction fail when the server is unreachable —
which is exactly when the console is least able to re-fetch anything.

##### The reserve, and what it is actually protecting

**Saves are kilobytes. States are the cost, and they are larger than they
look.** Measured on this project's own cores:

| | |
|---|---|
| Game Boy (Gambatte) | 26,882 bytes |
| Sega 32X (picodrive) | 679,178 bytes |
| Nintendo DS (melonDS) | **6,526,677 bytes** |

States deliberately do not overwrite — a history is the point — so one
well-played DS game can accumulate hundreds of megabytes on its own, and PS2
will be worse.

**A fixed reserve rather than a percentage is right**, because saves do not
scale with disk size — a 4 TB drive does not generate more save states than a
32 GB one, the same person plays the same games. That is the one place in this
section where a constant is the correct shape.

**But it should be sized for what is actually irreplaceable, which is far less
than it looks.** Saves, memory cards and save states all live on RomM once they
have been uploaded, and the local copies are caches of the server exactly as the
ROMs are. Cabinet already treats them that way — its own note says state caching
is "opportunistic, not queued", refreshed on ordinary online visits.

> **Nothing on this machine is irreplaceable except the upload queue.** Not the
> ROM, not the memory card, not the state history. Only what has been written
> and not yet sent.

That is a pending queue and room to write one more state — which is small on an
ordinary evening and **is not small on a bad one**. See *save-scumming while
offline* above: 360 states in two hours at 6.5 MB each is 2.3 GB of queue, and
PS2 is worse.

**So the floor is 2 GB, or 5% of the disk, whichever is smaller** — and it is
chosen knowing it does not cover that case, because **no floor can.** A reserve
protects one kind of data from another; it cannot protect data from itself. The
floor is sized to keep an ordinary session safe and to stop a download filling
the disk under a save, which are the failures it can actually prevent.

The other one is a conversation rather than a number, and it is recorded above
as unsolved.

**And keeping a game must respect it too.** Kept games are never evicted, so
without this check a person can keep enough games to starve the reserve and
leave the console with nothing it is permitted to delete. Keeping is the one
place the console may refuse — of two floors now, this one and the system
reserve above, and the second is the larger.

##### Why this differs from a real console, deliberately

A PS5 never evicts. It tells you the disk is full and makes you choose, because
an installed game is a thing you put there and removing it silently would be
hostile.

**Ours is a copy of something still sitting on the RomM server.** Evicting is
not destruction, it is spending bandwidth later. That is the streaming-device
model rather than the console one, and it is the right one here.

> The console-like property being preserved is **that nobody has to think about
> storage** — not that deletion must be manual.

The Storage screen shows what is cached, what is kept, what is used and what is
free, and **lists what was cleared to make room** rather than letting things
vanish. Anything the person cared about was already protected by keeping it.

##### The numbers, decided

**These are decisions, not proposals.** They cannot be improved by more
thinking: settling them properly needs a full disk on real hardware, which does
not exist yet, and a starting value that gets corrected by a measurement is
strictly better than an argument that blocks the work. Build with these, change
them when a machine says otherwise, and record the reversal here when it
happens.

| | | |
|---|---|---|
| Ignore candidates below | **nothing — deferred**, then 1% of the space being freed if it is ever needed | oldest-first is what ships; the refinement waits for evidence |
| Free beyond what is needed | **10%** of the budget | so eviction is an occasional event rather than every launch |
| System reserve | **room for a full image**, past 5 GB — checked when KEEPING a game, never against the cache | the cache is the system's to take; kept games are what can make an update impossible |
| Save floor | **2 GB, or 5% of the disk, whichever is smaller** | it protects the upload queue and room for one more write, not the state history — those are on RomM |
| Unpacking headroom | **the archive + what its index declares**, transient | read from the archive, never estimated; not needed at all for `.chd`, `.rvz` or an arcade set |

**The floor is smaller than the five gigabytes first proposed** because of what
it turned out to be protecting. Five was sized for a full local state history,
and a state history is a cache of RomM like everything else.

##### Genuinely still open, and neither blocks building it

- **A long spell offline defeats the floor**, because the upload queue is itself
  the thing filling the disk and no reserve can protect data from its own
  growth. That is a "this needs to reach the server" conversation rather than a
  storage rule, and it belongs with whatever handles being offline for a week.
- **Per location, not global.** Open question 14 already says the cached/kept
  distinction applies per storage location. The budget, the floor and the
  eviction pass are all properties of the *active* location; this section is
  written as though there is one, and it should be read that way until there
  are two.

##### Two things to inherit rather than rediscover

- **Download All sizes up front and refuses**, rather than filling the disk and
  letting eviction sort it out — which would evict what it had just fetched.
  Cabinet's `DownloadAll.swift` already does exactly this.
- **The one failure the person ever sees** is "the disk is full of things you
  asked me to keep". Its wording belongs with the Storage screen, and the screen
  it points at already exists in the design.

#### A platform is not its slug, and "Arcade" is two platforms

**Measured against the live server 2026-09-14**, on RomM 5.1.0 with read-only
device-token access — 35 platforms, about 1,600 ROMs. The library contains two
platforms that are identical in every field a client would naively key on:

| `id` | `name` | `slug` | `fs_slug` | ROMs |
|---|---|---|---|---|
| 22 | Arcade | `arcade` | **FBNEO** | 141 |
| 45 | Arcade | `arcade` | **MAME2003** | 82 |

Same `slug`, same `name`, different `id` and `fs_slug`. **A client keying
platforms by `slug` silently loses 82 games**, and one keyed by `name` shows the
user two entries called "Arcade" with no way to tell them apart.

- **Key by `id`.** It is the only field that is actually unique.
- **Route to a core by `fs_slug`**, which is what carries the intent.
- **Take the display name from the manifest's `systems` field**, which already
  disambiguates them: *"Arcade (FinalBurn Neo)"* and *"Arcade (MAME 2003-Plus)"*.

##### "A core exists" and "this console has it" are different questions

**Found 2026-09-15 by using it.** After exiting a game, Home's hero was
*Mushihime-sama Futari* — an arcade game that cannot start, because FBNeo has
not been built. `catalog::coverageFor` was answering from Cabinet's manifest,
which says a core exists for arcade, and the console had no such `.so`.

So `Support` now carries **`NotInstalled`** beside `NoCore` and `Excluded`, and
a Playable answer is downgraded when the core file is not on disk. Three
distinct reasons a game is absent, and the difference is the whole point:

| | |
|---|---|
| `NoCore` | nothing in the manifest serves it — Jaguar, ColecoVision. Permanent. |
| `Excluded` | a core exists and Cabinet does not ship it — Game & Watch. Deliberate. |
| `NotInstalled` | **this console has not built it yet.** Temporary, and today it is most of them. |

**With two of twenty-one cores built: 217 playable games of 1644.** That number
is the honest one and it is the argument for the remaining cores. A console must
not offer what it cannot run — but collapsing "we haven't built it" into "you
can't have it" would have hidden how much of the library is waiting on work
rather than on a decision.

**This is deliberate on the server, not a scan artefact.** The split exists
because mame2003_plus was what ran on iOS, and FBNeo serves the companion
controller and light-gun cases. So the two arcade platforms are a real
distinction the library already makes, and **CabinetOS should surface them as
two arcade systems rather than merging them.** Both cores are pinned in the
manifest — `fbneo_libretro` at `2444fbe3`, `mame2003_plus` at `21256d24` — and
neither takes build arguments on any Apple platform.

It also sharpens the rule recorded under *Controls*: configuration is keyed by
**platform**, not by core. Here is the converse — two platforms that share a
name and a slug and must not share a core. Neither the core nor the slug
identifies anything on its own.

#### Cover paths are not URLs until they are encoded

**Found 2026-09-14, against the live server.** RomM returns cover paths with a
cache-busting query appended, and the timestamp in it contains a space:

```
/assets/romm/resources/roms/8/230/cover/small.png?ts=2026-02-08 21:13:30
```

curl rejects that outright — *"Malformed input to a URL function"* — so handing
the path straight to a fetch fails for **every cover in the library**. The
failure mode is what makes it worth writing down: an empty result reads as "this
cover failed", `ImageCache` draws nothing for a cover that failed, and the whole
library renders with no art and **not one error message anywhere**. A silent
total failure costs more to diagnose than a loud partial one.

`romm.cpp` encodes conservatively before fetching: anything already legal in a
URL is left alone — including a `%` that begins a valid escape, so an
already-encoded path is not encoded twice — and everything else becomes `%XX`.

**The general lesson, which is the reason this is in the specification rather
than only in a commit message: counting is not fetching.** The probe reported
`covers: 0 of 171 have art` and that number was *correct* — Game & Watch has no
art — while every cover on every other platform was broken. Two different
questions, and only one of them was being asked. The probe now fetches a real
cover and checks the bytes are actually a PNG or JPEG, because a 200 carrying an
HTML error page is equally useless to a texture upload.

**Not every platform has cover art**, and that is normal rather than a fault.
Game & Watch has none of 171. Any "no art" state in the UI has to look
deliberate, not broken.

#### Plain HTTP must work. This is a bug CabinetOS can simply not have

**Apple's App Transport Security refuses plain HTTP**, so a tvOS app talking to
`http://romm.local:8080` needs an explicit exception — and a self-hosted RomM on
a home LAN is *very often* exactly that. **CabinetOS has no ATS**, nothing on
Linux forbids plain HTTP, and the whole problem is therefore avoidable.

Avoidable, but only if it is not designed back in. Two ways it creeps in:

1. **Prefilling `https://` in the address field.** It reads as helpful and it
   pushes people toward a scheme their server does not speak. Removed: the
   field starts empty with `romm.local:8080` as the placeholder.
2. **Requiring a scheme at all.** The field should accept a bare host and port.

**What the client must do:**

- **Accept a bare host.** `romm.local:8080`, `192.168.1.50:8080`, `romm.lan`.
- **Probe rather than assume.** With no scheme given, try both and keep
  whichever answers — preferring `http` for an address that is obviously local
  (RFC1918, `.local`, `.lan`, a bare hostname) and `https` otherwise.
- **Remember which worked**, so the probe happens once.
- **Never refuse plain HTTP.** Not for a LAN address, not with a warning that
  cannot be dismissed.
- **Self-signed certificates: ask once, then remember.** A home server with
  HTTPS usually has a self-signed or internal-CA certificate, which is the
  second wall of the same kind. Silently accepting is wrong and silently
  refusing is worse; asking once about a server the person typed in themselves
  is the honest middle.

#### The screens, built 2026-09-16

Until this the machine was in good shape and almost none of the library was
reachable: 1100 playable games, and only the fifty Home happened to show.

##### The Library is a tile grid with a switcher, and it shows EVERY system

Four columns of 413x200 tiles on a 1920 canvas at an 80pt inset, which is what
"adaptive minimum 380" comes out as — a full-width row would leave a name at one
end and a count at the other with a third of the screen empty between them.
Platforms and Collections are capsule pills, selected at white 35% and focused
at white 25% and 1.06, both visible at once. Focus lands on the switcher the
first time and only the first time.

**The unplayable systems are on it, dimmed, saying why.** This is the screen
`catalog::coverageFor` was built for and the reasons had until now only ever
gone to stderr. All four answers appear: *No core for this system*, *Core not
built yet*, *Not shipped here*, *Needs a 3D core*.

Two things had to be settled to make that readable, and both are recorded
because they look like polish and are not:

- **A tile title gets two lines and breaks on hyphens as well as spaces.** On
  one line, "Nintendo 64" and "Nintendo DS" were both "Nintendo ...", and the
  two Arcades this project goes to some length to distinguish were both
  "Arcade (...". Hyphens matter on their own: "TurboGrafx-16" and
  "TurboGrafx-CD" contain no space at all before the part that tells them apart.
- **A tile's second line holds about sixteen characters beside a cover**, and
  twenty-three without one. `Coverage::reason` is a sentence, and a sentence cut
  to "no core in the ..." tells a person strictly less than nothing. So the tile
  takes `catalog::shortReason` and the launch screen, which has a column, takes
  the sentence.

**Playable systems sort first, then alphabetically.** One flat alphabet put
Atari Jaguar — which this console cannot play — in the first tile on the screen.

##### The launch screen, and Download as the one deliberate storage act

A full-screen cover with the artwork as its own backdrop, filled and blurred
with the scrim over it. Large Title, the platform and the size, then rows.

**The cache stays invisible and Download is not a cache control.** Pressing Play
fetches a game that is not here and says nothing about it. The Download row
means *put this game on the machine and do not take it away again* — which is a
KEEP, and it is the only place in the product where the console may refuse.

> **A deliberate download IS a kept game.** There are exactly two categories on
> the disk and the Storage screen names them: kept, which is deliberate and
> permanent, and the cache, which is automatic and evictable. A download a
> person asked for by name belongs in the first.

So the row reads **Download and keep**, and on a kept game **Remove download**.
**Superseded 2026-09-19 — see open question 18.** Un-keeping now deletes the
game, because the row says "Remove download" and reclaiming space is why people
press it. The paragraph below is the original reasoning.

Un-keeping deletes nothing: the game returns to the cache, where it may sit for
months before anything needs the room.

##### The two floors, enforced where the button is

Both are checked before a byte moves, because refusing after two gigabytes is
the same answer at a much higher price.

The question is **not** "is there room right now" — a kept game may already be
on the disk, in which case keeping it costs nothing today. It is whether, after
this game stops being evictable, the console can still free its way down to both
floors:

```
reclaimable = free + everything still evictable (excluding this game)
                   - what remains to be fetched for it
                   - the upload queue
allowed     = reclaimable >= save floor + system reserve
```

**Measured on the test VM, 2026-09-16**, by filling the disk rather than by
reasoning about it: with 5.42 GB free against a 1.16 GB save floor and a 5.37 GB
reserve, keeping a 5 MB game was refused with 5.78 GB reclaimable against a
6.53 GB floor — and no directory was created, so nothing was fetched and thrown
away. The person is told the amount, because *"the disk is full of things you
asked me to keep"* is a dead end without one.

##### What eviction can and cannot take, now

- **A kept game is not a candidate**, enforced inside `cache::candidates` rather
  than at each caller, so no future caller can forget it. Verified: a kept
  game's ROM does not appear in the eviction list, and un-keeping puts both its
  files straight back into it. **Both halves of that changed on 2026-09-18 and
  -19 and got simpler:** a kept game is not a candidate because `candidates`
  only ever walks `cache/` and a kept game is in `roms/`, and un-keeping no
  longer puts anything back into the list because it deletes the game. Open
  question 18.
- **An unsent upload is a fact on disk.** A marker is written before an upload
  is attempted and removed only on success, so a queue interrupted by a crash is
  still visible on the next boot and its bytes still count against the save
  floor. Eviction never took save data, so this is not protecting files from the
  evictor — it is making "unsynced" something the machine knows.

##### Still owed on the launch screen

A different save state, a different core and an export. The screen is the right
home for all three and none is built; a row that does nothing is worse than no
row, so none is drawn.

##### The navigation bar is NOT built, and the reason is a measurement

The design system specifies four destinations in a bar across the top. It is not
there, and the Library is reached with a temporary key.

**Home has about 85 points of vertical slack and the bar needs about 85.** Hero
at 40 + 420 + 20, Recent's block at roughly 515, against a 1080 canvas. A bar at
Title 3 plus its gap consumes very nearly all of it, which would put Recent's
caption exactly on the bottom edge — and a physical television's overscan eats
more vertical room than a framebuffer capture shows. That is the trap Cabinet's
hero fell into three times, once while the simulator showed it fitting.

**So this needs the SER5 and a real panel, not a decision.** Either the bar
fits, or Home's hero comes down, or the bar lives somewhere else.

#### Core options: every one of them was unanswered — fixed and measured 2026-09-16

**This is the thing docs/PROJECT.md had warned about twice and said nobody had
checked. Nobody had, and it was worse than the warning.**

`gOptions` in `core.cpp` was declared, read on every `GET_VARIABLE`, and
**never written to by anything**. Above it sat a comment saying an absent key
meant "falling back to the core's own default" — which is the exact belief this
document says is false. The core does not fall back. It skips the case, and its
C global keeps whatever it was initialised to.

The tables were being thrown away too: `SET_VARIABLES` and all four
`SET_CORE_OPTIONS` variants were accepted and ignored, so nothing even knew
what each core could be asked about.

##### What the audit found

`--core-options` loads every built core and prints what it declares and what it
is answered with.

> **526 options across twenty cores. All of them previously unanswered.**

| | |
|---|---|
| Most options | Flycast 89, Mupen64Plus 83, Genesis Plus GX 62, pcsx_rearmed 54 |
| Fewest | Beetle NGP 1, prosystem 4, vecx 5 |
| **Declared none at core-load time** | **FBNeo, fceumm, MAME 2003-Plus** |

**A core declaring zero options is the suspicious case, not the clean one.**
MAME 2003-Plus declares 23 the moment a game is loaded and none before it — its
options are per-driver, so the table does not exist until a machine is chosen.
The audit says so where it used to say nothing.

##### The control, run rather than assumed

`--core-options-off` restores the old behaviour so the difference is measured
rather than asserted — the same discipline `cores/backend-diff.sh` exists for.

| Core | Answered | Control |
|---|---|---|
| MAME 2003-Plus | **48000 Hz** | **44100 Hz** |
| mGBA | 65536 Hz | 65536 Hz |
| pcsx_rearmed | 44100 Hz | 44100 Hz |

**MAME was running at a sample rate nobody chose.** The other two are unchanged
*in this probe*, and that is the part worth keeping: `av_info` reports geometry,
frame rate and sample rate and nothing else, so it cannot see the other five
hundred options at all. **The narrowness of the only probe we had is why this
went unnoticed.** Do not read "no difference in av_info" as "no difference".

##### A second finding: options asked for that were never declared

MAME 2003-Plus queries options that are not in the table it declared for the
loaded driver, and **which ones varies by game**:

| Game | Asked but never declared |
|---|---|
| 280 Zzzap | `nvram_bootstraps`, `four_way_emulation`, `crosshair_enabled` |
| Lethal Enforcers | `nvram_bootstraps`, `four_way_emulation`, `dialsharexy`, `dial_swap_xy`, `cheat_input_ports` |

`crosshair_enabled` IS declared for the light-gun driver and is not for the
driving one, which confirms the mechanism. Two are constant across both.

**These cannot be answered honestly from the core**, because it never states
their values or defaults for that driver — so they still fall through to zero.
They are the first real customers for `catalog::optionOverrides`, and the value
has to come from the core's source with a reason recorded beside it. **Do not
guess them.**

##### What the host does now

- Captures whichever generation of the declaration API a core uses:
  `SET_VARIABLES`, `SET_CORE_OPTIONS`, `SET_CORE_OPTIONS_V2` and both `_INTL`
  variants. The US table is the one read; `local` is the same table translated.
- **Reports core options version 2 rather than 0.** At version 0 a core falls
  back to the original API where the default is "whichever value is listed
  first" — a convention we would be inferring. At version 2 the core states its
  default outright. Cores that only speak the old API still call
  `SET_VARIABLES` and are handled.
- Answers every declared key with that default, or with an override.
- Records and reports any key asked for that was never declared.

##### Overrides are deliberately empty, and that is not the old behaviour

`catalog::optionOverrides` returns nothing today. Every option is still
answered — with the core's own stated default, which is the correct baseline and
is precisely what was missing. An override is for when CabinetOS wants something
*other* than what a core ships with, and Cabinet's hand-picked per-platform
subset (`NativeCoreOptions.swift`) is the obvious thing to bring across, one
platform at a time, with a reason beside each.

> **It has one entry now, 2026-09-17, and the first one found a hole.**
> PPSSPP's CPU backend is an option rather than a build flag, so
> `ppsspp_cpu_core` is answered with Cabinet's `IR JIT` — and adding it revealed
> that `optionOverrides` was being called **only by `--core-options`**, the
> audit, and by nothing that starts a game. While the table was empty that was
> invisible, and the audit agreed with itself. It is wired into both launch
> paths now.
>
> The lesson is the same one this section is about, one level up: **a table that
> is printed rather than applied is worse than no table**, because the
> instrument reports the intention instead of the behaviour.
>
> The audit was also not setting the core's directories, which is why PPSSPP
> warned that its system files were missing during a run meant to describe what
> the core does in the product. An instrument that sets the core up differently
> from the way the product does is measuring something else.

#### A bug worth keeping: an offscreen render composited to the window

`Renderer::presentScene` bound framebuffer 0 unconditionally, so with
`--render-size` the finished frame went to the WINDOW while `saveFrame` read the
offscreen target. A 1920x1080 capture came back as the 1024x768 window's
contents in the corner of a black frame.

It only appears when both are in play — an offscreen render AND frosted glass —
which is why 2026-09-13's three-resolution check did not find it: Home's glass
is one band at the bottom of the hero, and the failure reads as a layout
problem rather than a target problem. **The tool this project uses to prove a
4K layout was quietly broken for every screen with a pill or a panel on it.**

The renderer now remembers where the frame is going.

#### Headless capture, which CI can also run

`SDL_VIDEODRIVER=offscreen` gives the frontend a GL context with no compositor,
so every screen can be photographed on a machine with nothing running — no cage,
no session, no controller. Combined with `--screen`, each screen opens by
walking the route a person would walk rather than by being constructed directly,
so a capture cannot show a state the product is unable to reach.

```
--screen library [--tab 1] [--tile N]
--screen grid --tile N
--screen detail --game <romId>
--storage                       what the disk holds, and what may be evicted
--download <romId>              what the Download row does, guards and all
--unkeep <romId>
```

*Done when* the real library is browsable, a game downloads and plays, and a
kept game survives a cache eviction.

### Phase 5 — Emulators and launching
**Status: not started.**

Cores and emulators bundled into the image. Games launch and return cleanly with
no visible desktop. Controller mapping per system, per-system configuration, save
states wired to the sync layer.

Every system runs in-process, matching Cabinet — including PS2 and GameCube,
which Cabinet embeds as real PCSX2 and Dolphin rather than as libretro cores
(open question 12). One overlay implementation, one save state path.

The work here is open question 13 — building the same cores at the same
revisions for Linux x86-64 — not choosing an architecture.

**Order, from Phase 0's scoping.** The instinct is to build all twenty-one cores
and then find out. Do the opposite:

1. ~~**One core, at a pinned SHA.**~~ **DONE 2026-09-13, on the VM rather than
   in CI.** Gambatte: pinned, built, loaded, run, with Dr. Mario on screen and
   audio out. It covers two platforms, Game Boy and Game Boy Color. **Still
   owed: the same thing in CI**, so it is not a thing that works on one
   machine — which is the exact failure this whole open question is about.
2. ~~**One hardware-rendered core.**~~ **DONE 2026-09-16.** Flycast, because it
   is also Dreamcast and Naomi, and because it is the one that proves the GL
   context and the no-readback path — and it did: Ikaruga runs, in the same
   context the UI draws in, with nothing read back. Mupen64Plus came with it,
   so N64 runs too. See *Video: two paths* and open question 13.
3. **One backend-sensitive core.** pcsx_rearmed, built twice — `DYNAREC=0` and
   the Linux default — with a state written by each loaded by the other. That
   answers the parity question locally even if the Mac↔Apple TV test never
   happens.
4. **Genesis Plus GX next**, before the rest: software, no recompiler, and the
   best coverage in the set — one build is Genesis, Sega CD, Master System and
   Game Gear. Confirm each of those four separately, per the rule above; Sega CD
   in particular writes its saves by a different mechanism than the other three.
5. ~~**The rest**, which by then are a loop.~~ **DONE 2026-09-17, with PPSSPP.**
   All twenty-one libretro cores are built, and every one of them can be run.
   PPSSPP was left until last on the grounds that it needed the hardware-render
   path, and it did — but the thing that actually took the time was none of
   that: its CPU backend turns out to be a runtime OPTION rather than a build
   flag, its firmware ships with the emulator rather than coming from RomM, and
   running it found three host bugs that twenty cores had not. See open
   question 13.
6. **Dolphin and PCSX2 last**, as their own `.so` files, against upstream PCSX2
   rather than the ARM64 fork.

#### The deploy — BUILT 2026-09-19

**"Cores and emulators bundled into the image" was the first line of this phase
and it was the last part of it to happen.** For a fortnight the cores were
built, pinned, asserted and run, and none of them was in the image. Neither was
the frontend. `cabinetos-session` ran:

```
APP="${CABINETOS_APP:-/usr/bin/sleep infinity}"
```

So a freshly installed machine took tty1, started a compositor, and drew
nothing — indistinguishable on a television from a machine that failed to boot.
The OS half updated itself properly the whole time, which made it easy to
believe the rest did too. **MMagTech asked the question that found it**: if we
install CabinetOS on the mini PC now, does the work we do afterwards just
arrive as updates? Half of it did.

| | | |
|---|---|---|
| `/usr/bin/cabinetos-frontend` | 868 KB | a program, where programs go |
| `/usr/lib/cabinetos/cores/` | 259 MB | architecture-specific `.so` files, so `/usr/lib` |
| `/usr/share/cabinetos/system/` | 13 MB | PPSSPP's fonts and lookup tables |

All three under `/usr`, which a bootc update replaces wholesale. **That is the
whole point**: it is what makes the frontend and the cores travel the way the
session service already did. Nothing here may go in `/var` — see below.

**Nothing is built by the image build.** `build-frontend.yml` and
`build-core.yml` already build these things, and the image build CALLS them.
That matters most for the cores: the entire value of `build-core.yml` is that
it checks out an exact pinned commit and reads that same revision back out of
the finished `.so`, so a second way of building a core would be a second way of
getting that wrong and the image would ship from the one nobody was watching.
It costs about ten minutes of wall clock per image build, and it buys an image
build that proves all twenty-one pins.

Two scripts, and each fails loudly where it is cheapest to fail:

- **`ci/stage-image-payload.sh`** collects the three things above into
  `image_payload/` and refuses if any is missing — in seconds, before the
  thirteen-minute image build starts, naming the core. The list of cores comes
  off `cores/build-core.sh`'s own case arms rather than a second copy of it.
- **`build_files/install-frontend.sh`** installs them, then asks the question
  that decides whether the console starts at all: **can the binary, and each of
  the twenty-one cores, resolve every library it links against inside this
  image?** `ldd` prints `=> not found` and still exits 0, so it reads the
  output rather than the status. Run as a control into a bare `fedora:44` it
  names all six the frontend is missing plus PPSSPP's `libX11` and `libXext`,
  and exits 1.

That last check is worth more than the rest put together, because
`require-frontend-libs.sh` names only three libraries and the cores' own
dependencies have never been written down anywhere. Flycast alone wants zlib,
libzip, alsa and udev off the system.

##### `/var/lib/cabinetos` is created at BOOT, not at build

`storage::root()` tries `/var/lib/cabinetos` and falls back to the working
directory, which for a systemd service is `/`. The session runs as the
unprivileged `cabinet` user and cannot create a directory in `/var/lib`, so
without something making it the console would quietly fill the root of the
filesystem with somebody's games.

**The obvious fix is a `mkdir` in the Containerfile and it is wrong**, for
exactly the reason open question 21 records one layer along: bootc unpacks
`/var` from the INITIAL image only, and no later image touches it. A directory
created at build time would appear on machines installed after today and never
on machines that upgraded into it — green build, correct on the newest
machines, silently broken on the oldest.

`system_files/usr/lib/tmpfiles.d/cabinetos.conf` instead, which
`systemd-tmpfiles-setup.service` runs every boot, before `multi-user.target`
and therefore before the session.

##### The two things a console cannot be told on a command line

Both are tried rather than assumed, and **both print what they chose**, for the
same reason the storage root does.

- **Where the cores are.** `cores/build` when that exists beside the working
  directory, `/usr/lib/cabinetos/cores` otherwise; `--core-dir` overrides both.
  Printing it matters because getting it wrong is not loud on its own: the
  console reports every platform as *"the core for this system is not built on
  this console yet"*, which reads as twenty-one broken emulators rather than as
  one wrong path.
- **Which RomM server it belongs to.** `--romm`, or `$CABINETOS_ROMM`, which
  the session script exports from `/etc/cabinetos/session.env` if that file
  exists. **Nothing is baked into the image.** This repository is public and
  somebody's LAN address does not belong in it, and an image with one server
  compiled in is useful to one person. A console with no such file still boots,
  onto the stand-in library. The first-run screen writes the same file when it
  is built — open question 15.

*Done when* several systems are playable end to end, and a save state written on
Apple TV loads on CabinetOS.

### Phase 6 — Real hardware
**Status: not started, but no longer blocked.** Until 2026-09-19 installing on
real hardware produced a black screen, because the image carried no frontend —
so "install it and look at it" was not a thing anybody could do. Phase 5's
deploy fixed that, and the reference machine arrived the same day.

Install on real hardware — the GEEKOM A9 Max is the reference machine. Performance
tuning, Bluetooth controller pairing, audio output, display and resolution
handling.

Ship **developer mode** as specified above: hidden, opt-in from Settings,
enabling SSH and SFTP, visibly indicated while active, and genuinely stopped
when switched off. This is what lets builds be pushed to a running console
without reflashing it, and what makes the project contributable by anyone other
than its author.

Also verify the input model on real hardware: a Bluetooth controller must pair
and wake the machine, and a USB keyboard must work if plugged in without being
required for anything.

**Controls are a bigger piece of work here than they were in Cabinet** — see
*Controls*. Three things in this phase have no equivalent in the reference
implementation and cannot be ported from it:

1. **A remapping screen**, because Linux accepts controllers SDL has never seen.
   It has to be drivable with the very pad whose buttons are wrong, so it must
   ask by position — "press the button below the others" — not by name.
2. **Bluetooth pairing**, over bluez and D-Bus. The first controller cannot be
   paired using a controller, so USB-first has to work and first-run setup has
   to say so.
3. **Player assignment** that is visible and survives a pad sleeping and
   reconnecting. Four-player arcade and the GameCube adapter make this real.

**Wired is the bootstrap, not the fallback.** A first-run screen should offer
USB as the thing that simply works and Bluetooth as the convenience, not the
other way round. USB is deterministic; Bluetooth on a cheap mini PC is the part
that might not come up at all, since some combo cards need firmware blobs.
Three first-run states, not two: searching, found, and **no adapter — plug
something in**.

**Do NOT auto-pair the first gamepad discovered.** It is the obvious design and
it is wrong: the first advertisement in range may be a neighbour's pad, the
user's own second controller, or one belonging to the console beside it, and
pairing it silently gives no clue what happened.

> **Pair on a button press, not on discovery.** Show what was found, and let the
> pad that sends the first input become player one. The confirmation is the very
> input being established, which is why it costs the user nothing and cannot
> pick the wrong device.

And the discovery filter is less clean than it sounds. Class-of-Device
peripheral/gamepad catches DualSense, DualShock and most 8BitDo pads.
**Xbox controllers are the awkward case** — a proprietary Bluetooth profile
rather than plain HID, historically finicky under BlueZ. Several pads also only
enter pairing mode on a held button combination the user has to know, so the
screen has to name it per brand or at least say "hold the pairing button".

*Done when* the machine is usable from the sofa with a controller alone, and
reachable over SSH when developer mode is on.

### Phase 7 — Unified updates
**Status: not started.**

A release manifest carrying one version number for the whole system. The UI
checks, shows a console-style update screen, pulls, and reboots.

**Also in this phase: HDMI-CEC mode selection in Settings.** CabinetOS removes
the terminal, and with it `ujust` and the Bazzite Portal — both of Bazzite's
mechanisms for choosing between legacy and native CEC. Without a CabinetOS
setting, the choice is unreachable on a finished machine, and CEC is a hard
requirement (see *Hardware*). The reference implementation is
`/usr/share/ublue-os/just/81-bazzite-fixes.just`, which survives in the image.

*Done when* a full system update happens without a keyboard, and the CEC mode
can be changed with a controller.

### Phase 8 — Heavy systems and polish
**Status: not started.**

PS2, GameCube, Dreamcast and Naomi tuned. Boot splash, branding, settings
depth, first run setup.

*Done when* it is something you would hand to someone else without explaining
anything.

---

## Open questions

Nothing here should be resolved by guessing. Each of these needs either a test
or a decision made deliberately.

### 1. How much of KDE Plasma can actually be removed?
**Raised: Phase 1. Unresolved.**

Phase 1 removes the display manager and the desktop *applications*, and
switches the default systemd target to `multi-user.target`, but does **not**
uninstall Plasma itself. The desktop becomes unreachable rather than absent.

Reasons for the caution:

- Bazzite's own `Containerfile` applies `dnf versionlock` to `plasma-*` and
  `qt6-*` on the Kinoite base, which implies those packages are load-bearing for
  the image build.
- Bazzite installs `plasma-foreground-booster-dmemcg`, which is part of its
  process-priority handling for games and has a Plasma dependency.
- `xdg-desktop-portal-kde` is the portal implementation on this base. Removing
  it without a replacement may break file dialogs and permissions for anything
  Flatpak-shaped later.
- A cascading `dnf5 remove` of Plasma on an OSTree-derived image risks an image
  that builds but does not boot — and we currently have no way to notice that
  before the first VM test.

**What the first build showed:** stripping removed 40 packages out of 2745. The
desktop applications this base was assumed to carry were mostly not there in the
first place. So the size argument for removing Plasma is weaker than it looked —
the remaining weight is Plasma and Qt themselves, and taking those out is the
risky part, not the part with obvious payoff.

Which reframes the question. It is not "how do we make the image smaller"; it is
"does leaving Plasma installed cost us anything real?" Candidate answers: attack
surface, confusion for contributors, and the chance that something in it starts
on boot and fights the CabinetOS session. The third is the only one that would
actually break the product, and Phase 2 will find out.

Resolve in Phase 2, once there is a booted image to validate against and once
the frontend toolkit is chosen (Phase 3) so we know what Qt and Wayland
libraries are actually needed. `build_files/strip-desktop.sh` contains a
commented-out candidate list to start from.

### 2. Does removing `steam` also remove the controller udev rules?
**Raised: Phase 1. RESOLVED by the first build — no, controller support is intact.**

`dnf5 remove --no-autoremove` did what it was meant to. The first green build
(2026-09-13) removed `steam` itself and left everything around it:

- `kmod-xone` — Xbox wireless — present.
- `kmod-gcadapter_oc` — GameCube adapter — present.
- `kmod-new-lg4ff`, `kmod-hid-tmff2`, `kmod-hid-fanatecff`, `kmod-t150-driver` —
  force-feedback wheels — present.
- `50-steam-horipad-controller.rules` and the rest of the udev input rules —
  present.

Still to confirm on real hardware: that a controller actually enumerates and is
usable. The packages being there is necessary, not sufficient.

### 3. `bazzite` or `bazzite-deck` as the base?
**Raised: Phase 1. RESOLVED: stay on plain `bazzite` and write our own session.**

Checked on the booted image. Plain `bazzite` has **gamescope** —
`/usr/bin/gamescope`, plus `gamescopectl`, `gamescopereaper` and
`gamescopestream` — which is the part that matters and the part that would have
taken months to build.

What it does **not** have is `gamescope-session-plus`: no
`/usr/share/gamescope-session-plus/`, no `gamescope-session*` binary, and the
only Wayland session offered is `plasma.desktop`. That harness is
`bazzite-deck`-only.

So the choice is real, and the answer is to write our own. `gamescope-session-plus`
is a shell harness whose substance is Steam bootstrapping, `steamos-manager`
integration and Steam Deck hardware handling — all of which CabinetOS would be
unpicking rather than using. Switching to `bazzite-deck` to get it would drag in
Steam, `inputplumber` and the SteamOS management layer, and constraint 4 says
the frontend is owned rather than skinned.

What CabinetOS actually needs is small: autologin, then gamescope, then the
frontend inside it. The pieces of `bazzite-deck` worth borrowing are its power
button handling and possibly `inputplumber`, and those can be taken
individually if Phase 6 wants them.

**gamescope runs in the Unraid VM.** `gamescope --backend headless` starts,
runs its child and exits cleanly, so the session plumbing is developable without
the SER5. Note the VM's virtual GPU is QXL — fine for the headless backend;
switching the VM to VirtIO-GPU would be worth doing before testing the DRM
backend.

### 4. Bundle libretro cores directly, or build on RetroDECK?
**Raised in the brief. RESOLVED in Phase 0: bundle directly, as `.so` files.**

Cabinet runs native cores **in-process**, which is the libretro shape.
RetroDECK is a curated set of *standalone* emulators behind ES-DE — someone
else's frontend, which constraint 4 rules out on its own, and a process-launching
model, which throws away the single overlay and single save-state path that
in-process buys (open question 12).

Phase 0 closed the remaining doubt. Every core has a working `platform=unix`
path producing `<core>_libretro.so` directly, so bundling is not merely
preferable, it is **less work than any alternative** — the Apple-only merge and
symbol-renaming apparatus disappears and nothing replaces it. See open question
13 and *The frontend toolkit* for the layout.

### 5. Anaconda ISO vs. a plain disk image for installing to real hardware
**Raised: Phase 1. TESTED FOR THE FIRST TIME 2026-09-19, installing the A9 Max.
It works, and it is not acceptable for anyone but us.**

CI produces both a `qcow2` (for the VM boot test) and an `anaconda-iso` (for
real hardware). The ISO installs correctly — the A9 Max went from bare metal to
a working console with it. **What it does to the person doing the installing is
the problem**, and MMagTech asked directly whether this would do for a release.
It would not. In the order they hit it:

1. The stick is branded **Bazzite**, not CabinetOS.
2. The boot menu offers "Test this media & install", and that check reports
   **FAIL on good media** — `Supported ISO: no`, aborting at 4.8% — followed by
   *"We do not recommend using this medium. System will halt in 12 hours."* The
   media was fine: `dd` had written the ISO's exact 6,064,252,928 bytes and the
   install from it succeeded.
3. Screens of kernel errors, including `amdgpu: Fatal error during GPU init`
   and MediaTek Wi-Fi firmware failures. See *The installer runtime carries no
   firmware* below — they are harmless and they look like a broken machine.
4. **Anaconda itself**: disk partitioning, a "reclaim space" dialog that means
   *destroy the Windows install*, root password policy, an administrator
   checkbox, and an Advanced dialog offering UID and GID.
5. **No Wi-Fi to choose from**, with nothing on screen saying why.

Against *Constraints and principles* item 3 — anything that could leave the
user stuck at a terminal is a bug — and the rule that a screen needing a
keyboard is a bug, this is not a near miss. A console that opens by asking
about UIDs is not a console.

**What to build, in value order.** None of it is hard; it simply was never
anyone's job:

- **Automate the install completely in kickstart** — `clearpart --all
  --initlabel`, `autopart`, the `cabinet` user created silently, no Users
  screen and no Network screen. Anaconda runs with zero interactive screens
  when the kickstart answers everything. That reduces five decisions to one:
  *this will erase this machine — continue?*
- **`quiet loglevel=0` on the installer's kernel command line**, plus a
  CabinetOS splash over it. Phase 2 already carries this item for the OS boot
  and nobody had it for the installer, which is the screen a person sees
  FIRST.
- **Remove or fix the media check.** It fails on good media today, which is
  worse than not having it.
- **Brand the ISO.** Same Phase 8 work as the boot splash.
- **Get firmware into the installer runtime** if `bootc-image-builder` permits
  it, so Wi-Fi exists during setup; otherwise say on screen to use a cable.

**The honest remaining gap**, which no amount of kickstart closes: the first
step is still a firmware boot menu, and that needs a keyboard. A `raw` image
written to the target's drive from another computer does not help — it is
worse for most people, since it means opening the machine or owning a
USB-NVMe adapter.

**Decide the rest in Phase 6**, but the direction is settled: keep the ISO,
automate it, and brand it.

#### The installer runtime carries no firmware — 2026-09-19

Worth its own note, because it looks catastrophic and is not. Installing on
the A9 Max, the installer printed:

```
amdgpu 0000:c6:00.0: early_init of IP block <psp> failed -19
... <dm> <gfx_v11_0> <sdma_v6_0> <vcn_v4_0_5> <mes_v11_0> all -19
amdgpu 0000:c6:00.0: Fatal error during GPU init
mt7925e 0000:c3:00.0: Direct firmware load for mediatek/mt7925/... failed with error -2
```

**It is the installer's environment, not the image**, and one observation
settles it: the installer also failed to load `gc_11_5_0_pfp.bin`, which *is*
in Fedora's `amd-gpu-firmware` and *is* in the CabinetOS image. A file that
should not have failed was worth more than all the ones that did. The
installer runs a stock Fedora kernel — `7.2.5-200.fc44`, not the image's
`7.2.4-ogc3.1.fc44` — and ships no firmware.

The CabinetOS image has what this machine needs: 677 amdgpu blobs including
`gc_11_5_0_*`, `dcn_3_5_dmcub.bin`, `sdma_6_1_0.bin`, `vcn_4_0_5.bin` and
`psp_14_0_0_toc.bin`, all three MT7925 Wi-Fi and Bluetooth blobs, and an
initramfs carrying 666 amdgpu firmware files plus `amdgpu.ko`, built
`hostonly=no`. The installed machine drives the GPU perfectly — see *The A9
Pro, measured*.

**Two wrong turns on the way, recorded so nobody repeats them.** The
base-bump note about `linux-firmware` going backwards 20260910 → 20260810 made
"old firmware" the obvious theory, and it was "checked" against an invented
filename rather than the one on screen. Then `psp_14_0_8` was misread off a
photograph of a rotated monitor and a second theory built on the misread
digit — no Fedora `amd-gpu-firmware`, including the newest 20260916, contains
any `psp_14_0_8`. **Do not diagnose hardware from a photograph. Get a shell
and read `dmesg`.**

### 6. `/opt` mutability
**Raised: Phase 1. Left at Bazzite's default.**

The ublue template offers making `/opt` immutable so packages can write there.
Some emulators may want `/opt`. Left alone for now; revisit in Phase 5 when we
know how emulators are packaged.

### 7. OS branding in `/usr/lib/os-release`
**Raised: Phase 1. Deferred to Phase 8.**

Changing `NAME` and `PRETTY_NAME` is cosmetic and safe. Changing `ID` is not —
`dnf` and the repo definitions resolve `$releasever` and repo paths from it.
Phase 8 owns branding; when it happens, change the cosmetic fields only.

### 8. How do we get a shell before Phase 6 ships developer mode?
**Raised: Phase 1. RESOLVED: SSH is on from the start.**

**Decision:** `openssh-server` is installed and `sshd` is enabled from Phase 1
onward. Developer mode (Phase 6) is about *closing* SSH by default and gating it
behind the toggle — not about opening it.

Reasoning: Phases 2 through 5 consist of booting images and finding out why they
did not behave as expected. Doing that without a shell is not a hardship, it is
a different and much worse project. Contribution has the same requirement.

**This creates a debt that Phase 6 must pay.** An image that boots with SSH
listening is correct for a development tool and wrong for a console handed to
someone else. The tracked obligations are:

1. Phase 6 must flip the default to off. **What replaces it is open question 9,
   which is now answered**, and it is not developer mode.
2. Until then, CabinetOS is a development artifact. It should not be installed
   on a machine exposed to an untrusted network, and the README says so.
3. Password authentication is the interim mechanism because it is the only one
   that works before there is a UI. **It is also the shipping answer** — see
   open question 9 for why key-based auth was dropped.

`build_files/enable-ssh.sh` carries the same warning next to the code.

### 9. How a person reaches their own files, and how it authenticates
**Raised: Phase 1 as "how is developer mode revealed". ANSWERED 2026-09-19 by
MMagTech, and the answer made the question smaller.**

#### The decision

**A console ships with nothing listening. Settings has an ordinary, visible row
that turns file access on. Turning it on shows the address, the user name and a
password the machine generated for itself. It gives SFTP, not a shell.**

Four sentences, and each one replaces something this project was going to
build.

#### Why, in MMagTech's own terms

> *"We have to account for the fact most people won't want this on by default
> for security purposes, and most will also probably never use them."*

That is the whole argument and it is the right one. The previous plan was
written from a developer's chair — SSH is how this project is built, so the
question got framed as *how does a developer get in* rather than *what does a
console do for the person who owns it*. Those have different answers and the
second one is the product.

#### What it deletes

**The hidden developer-mode toggle, entirely.** This question used to be two
questions and the first was *discovery*: a version-number press count, a hidden
entry in an About screen, a controller input sequence. All of that machinery
exists to conceal something dangerous. **Getting at your own saves is not
dangerous, it is a feature** — the whole of open question 18's layout was
designed so that somebody logging in over SFTP could find their files without
being told where they are. Concealing it protects nobody and stops exactly the
people who need it from finding it. So there is no ritual, no press count, and
nothing to document about how to reveal it. It is a row in Settings.

**Key-based authentication.** The previous answer was "likely public key only,
with the key supplied through the UI or fetched from a GitHub username". Two
reasons it goes:

- **Enrolling a key with a controller is the thing that would make this
  unusable.** A generated password is six characters read off a television; a
  public key is not something anyone is typing on a d-pad, and fetching one
  from a GitHub username makes a local file transfer depend on an internet
  service and an account the owner may not have.
- **It is not proportionate to the threat.** The exposure is somebody on the
  same home network. A per-machine generated password, off unless switched on,
  and displayed only to whoever is in the room, is the same posture as a NAS
  and better than most. The image publishes no credential either way.

**Half of the "is the image dangerous" worry**, which was overstated and is
worth recording because it was believed for a day. `disk_config/disk.toml` sets
`cabinet` / `cabinet` and is in a public repository — but it only builds the
**VM's qcow2**. A console installed from the real image gets its account from
`system_files/usr/lib/sysusers.d/cabinetos.conf`, and sysusers creates it
**locked, with no password at all**. So the published image has never shipped a
published password. The real gap was the opposite: on a real console nobody
could log in, including its owner.

#### What it keeps, and it is the only thing to build

**The screen.** It was item 4 of four on Phase 6's list — *"surface in the UI
that SSH is listening whenever it is"* — and it turns out to be the whole
answer rather than a footnote, because it is also where the password lives and
where the switch is. One place that tells the truth: on or off, the address,
the user name, the password.

#### File access, not a shell

**"Copy my saves off" and "give me a root shell on the console" are different
asks with very different risk, and only the first belongs in Settings.** sshd
can restrict an account to the SFTP subsystem, and that is what the switch
turns on.

A shell stays on the development image, which already identifies itself with
`/usr/share/cabinetos/DEVELOPMENT-IMAGE`. That distinction exists in the build
today; Phase 6 uses it rather than inventing a second one.

#### Still to decide when it is built

- **Whether the machine advertises itself over mDNS** so `cabinetos.local`
  works and nobody has to read an IP address off a screen. Carried over from
  the original question and still open. It is the difference between typing
  `sftp cabinet@cabinetos.local` and copying four numbers by hand.
- **Whether sshd binds to the LAN only.** Also carried over. It matters less
  now that the default is off, and it is still the correct belt.
- **What the generated password looks like.** RomM's own pairing code is the
  obvious shape to copy — this console already shows one and people already
  read it off a television.

#### When

**With the Settings screen, on the reference machine.** It is a screen, so it
waits: see *Decided 2026-09-17: the look waits*. Nothing about it is buildable
usefully before then, because a password nobody can read is no better than no
password, and the development image must keep its shell until the day the
project stops being built over SSH.

### 10. Waking the machine, and turning the TV on
**Raised: Phase 1. Largely DECIDED — HDMI-CEC is a requirement. Phase 6 tunes it.**

Superseded by the hardware requirement above. CEC is no longer "use it if the
machine happens to have it"; the console turns the television on and is woken by
it, and a USB adapter is part of the bill of materials because essentially no
x86 mini PC wires the CEC pin.

What remains for Phase 6, with real hardware and a Pulse-Eight adapter present:

- **Legacy or native mode in practice.** Legacy is the expected answer — it is
  the adapter path, and `cecd` is known to interfere with wakeup on exactly this
  kind of dongle setup. Confirm rather than assume.
- **Suspend versus display-blank.** `cecd`'s config carries `suspend_tv` and
  `allow_standby`; the legacy path uses the `cec-onsleep` and `cec-onpoweroff`
  services. Which combination gives a clean "press the button, everything wakes"
  is an empirical question.
- **The controller path is still separate.** CEC handles the television.
  Bluetooth wake-from-suspend for a controller remains unreliable in general, so
  a machine that stays awake and blanks its display is still the likely default.

### 10b. The console never sleeps, and never blanks the screen
**Raised by MMagTech 2026-09-20, for a later discussion. NOT DECIDED — this
records what the machine does today and why it is not an accident.**

> we currently have no screen or sleep behaviour, the console just stays active
> all the time

**Measured on the A9 the same day**, rather than inferred:

| | |
|---|---|
| `IdleAction` | `ignore` — systemd's default, never changed |
| `IdleHint` on the session | `no`, permanently — **nothing is even watching** |
| The connected output | `card1-HDMI-A-1`, `dpms=On`, after 10 hours 46 minutes lit |
| `upower.service` | masked by us — *"mains powered; nothing to monitor"* |
| `ds-inhibit` | running, kept deliberately |

So it is not that idle handling is configured badly. **There is none at all**,
and no part of the system is measuring idleness for anything to act on.

**THE HARDWARE CAN DO IT.** Every connector exposes a `dpms` node and the
inactive ones read `Off`, so blanking is a write away. Nothing is missing except
a decision and something to make it.

#### Two things already in this document point at it

- **Open question 10 assumes this half-exists.** It says *"a machine that stays
  awake and blanks its display is still the likely default"* — the POLICY is
  half-decided and the MECHANISM was never built. That gap is this question.
- **`ds-inhibit` is kept on purpose**, and its whole job is stopping controllers
  from being counted as keyboards *for idle purposes*. The image carries a
  service that exists to make idle detection behave correctly, on a console that
  does not detect idle. One of those two decisions is wrong.

#### What makes this harder than it looks, and why it is worth a session

- **A console is not a PC and not a television.** Suspending is wrong if a
  controller cannot wake it — and Bluetooth wake-from-suspend is unreliable in
  general, which open question 10 already records. Blanking the display is the
  safe half; suspending the machine is the half that can strand somebody.
- **It collides with CEC.** The console turns the television on and is woken by
  it. Blanking our own output while the set stays on, or letting the set sleep
  while we stay lit, are different behaviours and only one of them is right.
  `cecd` carries `suspend_tv` and `allow_standby` for exactly this.
- **A game running is not idle even when nothing is pressed.** Somebody
  watching a demo attract loop, or thinking about a puzzle, must not have the
  screen go out. Whatever measures idleness has to know a core is running.
- **And burn-in is a real cost on the panels this ships to.** A static frontend
  left on an OLED for ten hours is not a neutral default, which is what the
  measurement above is.

**Nothing is blocked on this** — it is a console that stays on, which is
survivable and honest. It wants a session of its own, with the CEC work, because
the two answers have to agree.

### 11. NVIDIA hardware
**Raised: Phase 1. Out of scope until there is hardware that needs it.**

Bazzite publishes `bazzite-nvidia` alongside `bazzite`, so supporting an NVIDIA
machine is a base-image change rather than a rewrite. The cost is real though: a
second image to build, sign, test and boot on every change, and NVIDIA driver
breakage is the single largest maintenance tax on custom Bazzite images.

Not doing it now. If the hardware changes to something NVIDIA-based, the work is
to parameterise the base image in the `Containerfile` and matrix the build
workflow over both variants — not to restructure anything.

### 12. In-process cores for the heavy systems, or separate processes?
**Raised: Phase 1. RESOLVED by reading the Cabinet repository: in-process, all of them.**

I had assumed the heavy systems would need standalone PCSX2 and Dolphin in their
own processes, because their libretro cores lag the standalone emulators badly.

That was wrong, and the answer Cabinet already uses is better than either option
I had considered. **Cabinet embeds the real PCSX2 and Dolphin as static
archives** — `libpcsx2_mac.a`, `libdolphin_mac.a`, built from source with
`tools/build-pcsx2-mac.sh` and `tools/build-dolphin-mac.sh` plus patch scripts —
rather than using their libretro cores. Full emulator quality, still in-process.

**Consequences, all good:**

- **One overlay implementation, not two.** The frontend owns the frame loop for
  every system. No gamescope compositing tricks, no per-emulator overlay.
- **One save state path.** The frontend owns state for every system, and hands
  it to the RomM sync layer the same way each time.
- **No process launching at all**, which removes an entire category of "returned
  to a desktop for half a second" bugs from Phase 5.

**And the work is easier here than it was on macOS.** Dolphin and PCSX2 both
target Linux x86-64 as a first-class platform. The Mac build needed patch
scripts to get there; the Linux build should need fewer, or none.

**Phase 0 confirmed that, with a number.** Reading both patch scripts: roughly
five of Dolphin's eight edit groups and nine of PCSX2's seventeen are Apple or
Metal walls that simply do not exist on Linux. The remainder are not port work at
all — they are the frontend claiming the audio, the input, the on-screen messages
and the present path, which it has to do on any platform. And the host layers
Cabinet wrote are already plain portable C++: 574 lines for Dolphin, 811 for
PCSX2, with only 155 and 681 lines of Objective-C++ beside them.

**One correction to this entry.** It says "in-process, all of them", and that
remains right about *who owns the frame loop* — but on Linux "in-process" should
not mean "statically linked". Each emulator becomes its own `.so`, loaded behind
the same struct of function pointers the libretro cores use, so the frontend
binary stays small and a core bump moves one image layer instead of all of them.
Same process, same frame loop, same overlay; different linkage. See *The frontend
toolkit*.

**THE `.so` HALF OF THAT CORRECTION IS NOW MEASURED, 2026-09-21.** It was an
assumption until then, and it had a way of being wrong that nobody had checked:
a static library built without position-independent code cannot be linked into a
shared object at all, and the failure comes at link time with a relocation error
rather than anywhere useful. Upstream PCSX2 defaults `POSITION_INDEPENDENT_CODE`
to ON, the build passes it explicitly rather than relying on that, and the whole
emulator does link into a 28 MB `.so`. It does not yet `dlopen`, and the reason
is the right one: the host layer that would resolve its 57 remaining symbols has
not been written. See `docs/PCSX2-HOST-SURFACE.md`.

**Upstream also answers the harder half of this entry for free**, which the
original reasoning did not anticipate: `pcsx2-gsrunner` is a complete, in-tree,
Linux-native, Qt-free frontend for the same library. Open question 12b.

The remaining work is open question 13 — producing Linux builds of the same
cores — not an architectural choice.

### 12b. Every platform audited, and the order the rest get built
**Audited 2026-09-20 against the running console and against Cabinet's own core
manifest. The ORDER is MMagTech's decision, the same day.**

36 platforms on the reference server, 1650 games. **1147 play; 503 do not.**

#### CabinetOS ships exactly Cabinet's tvOS core set

All 21, no gaps — the core-parity constraint holding. Read from Cabinet's
`docs/core-manifest.json`: of its 25 cores, `gw` and `vemulator` are iOS-only by
Cabinet's own decision, and `dolphin` and `pcsx2` are Mac-only. **Nothing tvOS
plays is missing here.**

#### The 332 games that are missing, in three tiers

Game & Watch is excluded from this table — see below — which is why it is 332
and not 503.

| | Games | |
|---|---|---|
| **An ordinary libretro core exists and nobody added it** | **73** | Atari Jaguar 48 (`virtualjaguar`), ColecoVision 25 (`gearcoleco`, `bluemsx`) |
| **Cabinet solved it on macOS and we have not** | **85** | PlayStation 2 71, GameCube 14 — open question 12, and there is a working reference to copy |
| **Nobody has solved it** | **174** | Switch 109, PS3 32, Vita 27, Xbox 4, Wii 2 |

**The first row is the cheap one and it is nobody's architecture problem.** Both
cores are on libretro's buildbot as ordinary `.so`s — checked, not recalled —
and they are excluded only because they are not in Cabinet's manifest. Cabinet
never played them on any platform, so this is the first place CabinetOS would
have more systems than its reference implementation. See open question 19.

#### THE HEAVY SYSTEMS ARE COMING TO THIS OS — decided 2026-09-20

> switch, ps3 and xbox will be brought to the OS because we have less
> constraints to work with in linux and more power. Same with Wii U if I get
> more games.

That settles a question this document has only ever discussed as a
recommendation. **The argument is the machine**: these were out of reach on
Apple hardware for reasons that are not reasons here — no JIT restrictions, a
real GPU, Vulkan, and a power budget a set-top box does not have.

**The order, and it is deliberate:**

1. **PlayStation 2 and GameCube first.** 85 games, and the only tier where a
   working implementation already exists to copy. Open question 12 has the
   numbers: roughly five of Dolphin's eight patch groups and nine of PCSX2's
   seventeen are Apple or Metal walls **that do not exist on Linux**.
2. **Then Switch, PS3 and Xbox**, largest first by library — Switch alone is 109
   games, more than the whole of tier one and two together.
3. **Wii last, because it has the fewest games** — 2. And it may arrive free:
   Dolphin does Wii as well as GameCube, so the tier-one work probably carries
   it. **Nothing should be spent on Wii on its own.**
4. **Wii U** if the library ever justifies it. Not in the reference library at
   all today.

#### Which PCSX2 to pin, and what its save states actually are

**PIN UPSTREAM `PCSX2/pcsx2`, NOT THE FORK CABINET USES.** Checked 2026-09-20:

| | |
|---|---|
| Cabinet's pin | `isztldav/pcsx2` @ `c89cb8ae`, **2026-07-06** — *"fix linux arm build"* |
| Against upstream | **384 ahead, 291 behind**, diverged from `master` 2026-07-04 |
| What the fork is for | Apple Silicon: *"ARM64: EE rec — fix FMV lag"*, *"only build macos arm"*, Qt/macOS keyboard and clipboard work |

**Almost none of that divergence is work this console needs**, and matching it
would mean deliberately shipping a staler, Apple-specific PCSX2 on the one
platform PCSX2 already supports natively. Dolphin needs no such decision: it is
already upstream `dolphin-emu/dolphin` @ `a1e636d7`, 2026-09-01.

**AND VERSION PARITY BUYS LESS HERE THAN IT DOES FOR THE LIBRETRO CORES**, which
is MMagTech's point and it is right. A PS2 memory card is the emulator's own
bytes — `Sony PS2 Memory`, 8,650,752, native — so it travels regardless of
build. And the STATES do not travel at all today:

- Cabinet's Mac save state is **PCSX2's own slot 1**, through
  `VMManager::SaveStateToSlot`, keyed by disc serial and CRC. Not a buffer, not
  uploaded, not tagged, not a history.
- GameCube is the same shape — `CabinetDolphinSaveState(slot)`.

**So "one save state path" above is an aspiration rather than a description of
Cabinet.** `PS2PlayerView` says so itself: *"PS2 shares no code with the libretro
path: not the frontend, not the renderer, not the audio."* Whatever CabinetOS
does here is new work, and nothing crossing between machines today constrains it.

#### MEASURED 2026-09-20: BOTH LIBRETRO CORES RUN HERE, AND BOTH NOW DRAW

Not a recommendation — the buildbot `.so`s were put on the reference console
and driven by this frontend. What was found, in order:

| | |
|---|---|
| **The catalog already routes them** | `catalog.cpp` has `ngc → dolphin` and `ps2 → pcsx2`, `Support::Playable`, and `coreFileName` resolves those to exactly the buildbot filenames. Two files in a core directory turned "not built on this console yet" into **85 playable games** with no code change at all. |
| **Neither has a missing library** | `ldd` against the real image names none, for either. No base bump, no new package. |
| **Both needed Vulkan and nothing else** | See open question 20. On GLES, PS2 drew nothing and GameCube drew nothing and sometimes crashed; on Vulkan both draw. |

**And Cabinet's own hosts are the same shape, which corrects open question 12.**
`CabinetDolphin::Run` and `CabinetPS2::Run` each say in their header that they
*"block for the entire life of the game and must be given its own thread"*, and
each emulator presents into a `CAMetalLayer` itself. So *"one overlay, one frame
loop"* describes the twenty-one libretro cores and **was never true of Cabinet's
PS2 or GameCube either**. Whichever route this console took, it had to learn to
host an emulator that drives itself.

#### Why Cabinet did not take the libretro cores — no written record, but the evidence is clear

| | |
|---|---|
| The PS2 libretro core is **LRPS2** | libretro's own words: *"a hard fork/derivative"* of PCSX2. Its own version number, v2.0.0. Not upstream with a shim. |
| Cabinet needed an **ARM64 recompiler** | which is why it pinned `isztldav/pcsx2`. Its commit message says the fork's ARM64 emitter was machine-translated, so *"the pin matters more than usual"*. LRPS2 would have hit the same Apple Silicon wall. |
| The Dolphin libretro core | `libretro/dolphin`, **396 commits diverged** from upstream and 62 behind. Its own core info admits it *"exposes only a subset"*. |
| Cabinet's release notes lead on | *"running PCSX2 and Dolphin with their real recompilers, at full speed"*, and PS2 *"gets its renderer remedy where you would look for it"* — per-game renderer, upscale and blending as real UI, not core options. |

**It was a quality and parity decision, not an architectural one, and it was
made on Apple hardware.** Neither binding constraint exists here: this is
x86-64, where upstream PCSX2's recompiler and Dolphin's JIT64 are the
originals rather than a translation. **That is why the question is open again
on Linux, and it is the thing nobody had written down.**

#### THE PS2 LIBRETRO CORE CANNOT BE SHIPPED — checked 2026-09-21

**`libretro/pcsx2` does not exist.** `git ls-remote` says *"Repository not
found"*, the API returns 404 while `libretro/dolphin` returns 200 in the same
second, and the only mirror was last pushed in **2020**. libretro's own recipe
still points at the dead URL and their buildbot builds from a checkout nobody
can obtain.

**A core whose source cannot be cloned cannot be pinned, built in CI, put in
the image, or audited.** That is not a quality trade-off, it is a hard stop,
and it is independent of every other argument on this page.

It is also years stale even if it could be had: it reports `v2.0.0-afbcc8a`
and carries the string `1.7.1`, against upstream PCSX2 **v2.8.2**.

**So PlayStation 2 takes the route this document already chose — embed upstream
`PCSX2/pcsx2`.** GameCube is different: `libretro/dolphin` exists, was pushed
2026-09-19, and is pinnable, so that one remains a real choice.

**WHAT THE LIBRETRO ROUTE COSTS, and it is not nothing.** The two cores follow
libretro's forks rather than Cabinet's pinned manifest, which is a real
exception to *Core parity is a hard constraint* and has to be recorded as one
rather than drifted into. A save state written by LRPS2 will never load in the
Mac's PCSX2. **The memory cards are the part that can still travel**, and they
are the part that matters — see below.

**AND THE MAC CORES ARE NOT LIBRETRO CORES, which is the thing to check before
assuming this is a build job.** `dolphin` builds from `dolphin-emu/dolphin` and
`pcsx2` from a fork, each with its own script — while all 23 other cores use one
shared `tools/build-core.sh` against a `libretro/*` repo. Both libretro
equivalents exist on the buildbot, and Cabinet did not take them. Nobody wrote
down why, and a dedicated builder for exactly the two systems that have a
libretro alternative is a decision somebody made after trying. **Read
`tools/build-dolphin-mac.sh` before believing otherwise.**

#### MEASURED 2026-09-21: UPSTREAM BUILDS AS A LIBRARY ON LINUX, WITH NO PATCHES

**The first question this route had was answerable in one build, and the answer
is the good one.** `docs/PCSX2-HOST-SURFACE.md` has the whole of it and
`cores/build-pcsx2.sh` reproduces it in 43 seconds from a clean clone.

**A BELIEF THIS DOCUMENT AND THE HANDOVER BOTH CARRIED IS WRONG, and it is
worth correcting rather than quietly dropping.** Both said *"PCSX2's CMake
builds an APPLICATION, not a library — Cabinet had to carve the frontend out"*.
It does not and Cabinet did not:

| | |
|---|---|
| `pcsx2/CMakeLists.txt` line 8 | **`add_library(PCSX2)`** — the emulator is a library target upstream |
| The application | a **separate** target, `pcsx2-qt`, behind `if(ENABLE_QT_UI)` at the top level |
| What `patch-pcsx2-mac.py` really does | replaces what **Catalyst cannot compile** — SDL3, cubeb, `CocoaTools.mm`, an `NSView`/`CAMetalLayer` seam, `pthread_jit_write_protect_np` through `dlsym`, Homebrew's FFmpeg. It never changed a target type. |

**None of those is a Linux problem.** `-DENABLE_QT_UI=OFF` and nothing else
produces `libpcsx2.a`, 35 MB, in 25 seconds on the A9, carrying the Vulkan
renderer (220 symbols), the OpenGL fallback (127) and the x86-64 recompilers
(318 for microVU alone). Zero Metal symbols, which is asserted rather than
assumed.

**AND UPSTREAM SHIPS A SECOND HOST LAYER TO READ.** `pcsx2-gsrunner` is 1332
lines in one file, has no Qt, implements the whole `Host` contract, links
against the library in 2.2 seconds and runs. It is Linux-native and maintained
in-tree, so unlike Cabinet's it cannot go stale against the version we pin.
**Read both**: Cabinet's `CabinetPS2Host.cpp` is the better guide to what a
console frontend wants, gsrunner to what this PCSX2 requires.

**THE HOST LAYER IS 57 SYMBOLS, COUNTED RATHER THAN ESTIMATED.** A shared
object links with undefined symbols and then fails at `dlopen` naming only the
first, so the count comes from `-Wl,-z,defs`, which makes the linker refuse and
name them all. 53 are in `Host::`; the other four are three
`InputManager::ConvertHostKeyboard*` functions and `g_host_hotkeys`, which is a
**variable** and is the one a `dlopen` trips on first.

**The contract is 55 and the linker asks for 53, so implement 55.** Cabinet's
`CabinetPS2Host.cpp` and upstream's gsrunner define the **same 55 `Host::`
functions, set-for-set** — nothing in either that the other lacks. The two the
linker leaves out, `GetTopLevelWindowInfo` and `InBatchMode`, are simply
unreferenced in this configuration and both reference frontends implement them.
The majority of the 55 are one-line stubs, because a console has no clipboard,
no file selector, no achievements login and no game list of PCSX2's own. **Six are the real job**, all in the display path:
`AcquireRenderWindow`, `ReleaseRenderWindow`, `BeginPresentFrame`,
`RequestResizeHostDisplay`, `IsFullscreen`, `SetFullscreen` — and open question
20's Vulkan host already owns the device, the queue and the crossing those need.

**TEN HAND-BUILT DEPENDENCIES BECOME ONE `dnf` LINE.** Cabinet cross-compiled
ten for Catalyst with pinned tarballs and SHA sums. Fedora 44 satisfies every
version constraint PCSX2 states — libpng 1.6.55 against a required 1.6.40, SDL3
3.4.0 against 3.2.6, plutovg 1.3.2 against 1.1.0, and so on. **The only gap is
`libbacktrace-devel`**, and `USE_BACKTRACE=OFF` disposes of it.

**What this does NOT show, stated plainly:** nothing has been emulated, no host
layer is written, and none of it is in CI — deliberately, because a PCSX2 build
in the image workflow before there is anything to ship costs every build minutes
and proves nothing the script does not prove on demand.

#### DECIDED 2026-09-21: ONE PATCH TO PCSX2, AND NO MORE

**MMagTech: "audio seems like we had to or it wouldnt have worked for the
others, id rather not have to patch and then maintain them."** That is the rule
now, and it is a rule about maintenance rather than about taste.

**The audio patch stands because there was no alternative.** PCSX2 picks its
output from a fixed list of backends with no plugin mechanism, so the choice was
three lines in `AudioStream::CreateStream` or letting PCSX2 open a sound device
of its own — a second volume, a second latency, and nothing the in-game overlay
could duck. Every other emulator on this console hands its samples to the
frontend, and PlayStation 2 now does too.

**WHAT THIS RULES OUT, EXPLICITLY.** Sharing PCSX2's Vulkan image with the
frontend instead of copying the picture through the CPU needs two optional
device extensions that upstream does not enable. That is a second patch, in the
graphics device setup rather than a one-line factory switch, and it is
**rejected on maintenance grounds** rather than on merit.

**The cost of that decision is larger than it first appeared, and the first
version of this paragraph understated it.** Measured 2026-09-21 UNCAPPED, 4x
cost 2.7 ms a frame. Measured again CAPPED TO 60 Hz, which is how a person
plays, the same thing costs **6.1 ms on average and 12.2 ms at worst** — 37% and
73% of a frame budget. Uncapped the emulator runs flat out and the readback
overlaps other work and hides inside it.

So the accepted cost is not "true 4K is expensive". It is that **4x already
spends a third of every frame moving the picture around**, and the only thing
that removes it is the patch this entry rejects. The lever that remains is the
upscale itself, and it is roughly proportional to pixels: 3x costs about half
of 4x.

**A MEASUREMENT TAKEN IN A CONFIGURATION NOBODY PLAYS IN IS NOT A MEASUREMENT
OF THE PRODUCT.** This project already had the warm-cache rule written down;
this is its other half.

**The reason this is the right trade rather than a reluctant one** is written
across this project already. Cabinet's Mac build carries 546 lines of patches,
and that is a large part of why `core-manifest.json` cannot honestly describe
how its cores are built — the builder script became the only real record. Every
patch is a thing to carry at each version bump, and this repository's own rule is
that a fact carried across is a fact nobody has checked.

**WHAT IS STILL OPEN, and it needs no patch at all:** the frame handover copies
about 22 MB per frame at 4x, under a lock PCSX2's own thread also wants. A
double buffer makes that a pointer swap and is entirely our own code. It is the
first thing to try against the stutter MMagTech saw, and if it is not enough the
next step is a measurement — how much of the readback is copying versus waiting
for the GPU — not a patch.

**And if upstream ever enables those extensions themselves, this decision is
free to revisit.** Turning on an optional extension where the driver has it is a
reasonable thing to propose to them; what is rejected is CARRYING it.

#### The saves, measured on the running console 2026-09-20

**Seven real rows already exist on the RomM server**, written by MMagTech
playing on the Mac: four PS2 tagged `pcsx2` and three GameCube tagged
`dolphin`. Not `-native` suffixed — those are the tags, and CabinetOS using
the same ones is what would let a card travel between the Mac and the console.

**Two things stand in the way, both found by looking at what the cores wrote
rather than by reading:**

- **PS2 writes the SHARED card.** `pcsx2_shared_memory_cards` defaults to
  enabled and produced `Mcd001.ps2` and `Mcd002.ps2` in the system directory.
  That is exactly the arrangement Cabinet's own `PS2MemoryCard.swift` rejects:
  *"a shared card belongs to no rom in particular"*. Per-game cards named the
  way the Mac names them is the work.
- **GameCube's format DIFFERS FROM THE MAC'S.** Ikaruga saved after thirty
  seconds of emulated time, to
  `User/GC/USA/Card A/70-GIKE-ikaruga_save_data.gci` — Dolphin's **GCI-folder**
  mode, loose files per save. The Mac writes a whole-card `.raw`, which is what
  the three rows on the server are. **As things stand they would not
  interchange.** It looks fixable without patching, because the core reads
  `User/Config/Dolphin.ini` and the slot device can be written before launch,
  but it is a decision rather than a detail.

**And one trap already visible in the options:** `pcsx2_analog_mode1` defaults
to **disabled**, which is the DualShock's analog mode off — the sticks do
nothing. Exactly the class of silent fault *An unanswered core option is not
the default* exists for.

#### Game & Watch will not be built — decided 2026-09-20

171 games, and **the largest excluded row in the library by a distance.**
MMagTech: *"we will not build that in the os as the games are too small on a
tv."*

It is a decision rather than a gap, and it is not the one Cabinet made — Cabinet
ships `gw` on iOS and macOS and excluded it from tvOS. The console's own reason
string now says why THIS machine will not play them rather than what Cabinet
chose on a phone, because 171 games is the tile most likely to be asked about.

### 13. Building the same cores for Linux x86-64
**Raised: Phase 1. Repo layout DECIDED. SCOPED IN PHASE 0, 2026-09-13 — most of
this is now answered. What remains is listed at the end and is small.**

**Decision: CabinetOS stays a separate repository from Cabinet.**

Reasons:

- Nothing is shared at the CI level. Cabinet builds with Xcode on macOS;
  CabinetOS builds a container image on Linux. A monorepo would mean path
  filters on every workflow to stop each toolchain running on the other's
  commits.
- Cabinet is roughly 270 MB because it commits built `.a` archives. Anyone
  cloning CabinetOS to work on the OS would pull a quarter of a gigabyte of
  Apple binaries they cannot use.
- Different release cadences — App Store submissions against OS images — and
  different contributors. Someone who wants to help with the console is not
  necessarily an iOS developer.
- Blast radius. A mistake in OS work should not be able to break the CI of the
  app that ships today.

A third repository holding just the cores was considered and rejected as
premature: it is real work, it disrupts a shipping app, and it leaves three
things to keep in step instead of two. Revisit only if the coupling actually
starts to hurt.

**The repo layout is not what guarantees core parity — a manifest is.** With one,
parity is enforced by data and the directory structure stops mattering. Without
one, a monorepo would not save it either.

#### The core manifest

Needed in **Cabinet**, because Cabinet is the app that ships and the source of
the constraint. Per core: upstream repository, exact commit SHA, which systems it
serves, and which platforms it is built for. `build-core.sh` should check out
that SHA instead of cloning `HEAD`, and record it on any bump.

CabinetOS then reads the manifest, builds the same revisions for Linux x86-64,
and asserts at build time that what it produced matches. A mismatch becomes a
failed build rather than a save state that silently will not load.

#### Scoped in Phase 0 — what the code actually says

Checked 2026-09-13 by reading Cabinet's build scripts and fetching all twenty
upstream libretro Makefiles. **Verified** means read from the source;
**assumed** means reasoned and not run. Nothing here has been built — no Linux
toolchain has touched any of it, because nothing builds on this Mac.

##### Every core has a Linux path, and it is the best-tested path they have

**VERIFIED, all twenty Makefile-based cores.** Each one has a `unix` branch, and
each one **defaults to `platform = unix` when `uname` says Linux**. This is the
standard libretro Makefile preamble, and it is the configuration RetroArch's own
Linux builds ship — far more exercised than the `ios-arm64` and `tvos-arm64`
cases Cabinet uses.

Every `unix` branch produces `<core>_libretro.so`. **So the Linux build is not
just possible, it is the simplest target Cabinet has**: `make platform=unix`
yields the artifact directly, with no merge step, no symbol renaming and no
wrapper.

The three CMake cores — mGBA, Flycast, PPSSPP — all carry a `LIBRETRO` option
and Linux handling, and all three ship official Linux libretro cores upstream.
**Assumed** to build; not compiled here.

##### The ARM-assembly worry is inverted

**VERIFIED.** Five cores touch assembly at all: FBNeo, GW, pcsx_rearmed,
mupen64plus and picodrive. In every case the assembly is gated behind ARM
architecture detection and **is simply not compiled on x86-64**. FBNeo's is Vita
only. GW's `linux_x86_64` is an explicitly supported upstream platform.
picodrive's ARM cores (Cyclone, DrZ80) are selected by `ARCH` and give way to
their C equivalents (FAME, CZ80).

So no core is blocked by ARM assembly. **The real risk runs the other way**, and
it is the important finding of Part 1:

> **On Linux x86-64, several cores turn ON a recompiler that the Apple build has
> OFF.** A build that just says `platform=unix` inherits a different CPU
> emulation backend than the one Cabinet ships — silently, with no warning, and
> producing a core that is *better* but not *the same*.

**VERIFIED**, per core, from the Makefiles:

| Core | Cabinet on tvOS | Plain Linux x86-64 build | Lever |
|---|---|---|---|
| **pcsx_rearmed** | `DYNAREC=0`, pure interpreter | `DYNAREC=lightrec` — a real recompiler, plus `LIGHTREC_CUSTOM_MAP=1` | `DYNAREC=0` |
| **melonDS** | no `JIT_ARCH`, interpreter | `JIT_ARCH=x64` — the x86-64 recompiler | `JIT_ARCH=` |
| **mupen64plus** | `WITH_DYNAREC=` (empty), `-DNO_ASM`, GLES3 | `WITH_DYNAREC=x86_64` (needs **nasm**) and desktop `-lGL`, not GLES | `WITH_DYNAREC= FORCE_GLES3=1` |
| **picodrive** | `APPLE=1` forces `use_sh2drc=0` | SH2 recompiler **on** (32X, Sega CD) | `use_sh2drc=0` |
| **Flycast** | `-DTARGET_NO_REC`, interpreter | full x64 SH4 dynarec | omit / keep |
| **genesis_plus_gx** | `HAVE_CDROM` left at its default of 0 | `HAVE_CDROM=1`, set by a `uname -s` test inside the `unix` branch | `HAVE_CDROM=0` |

**genesis_plus_gx was added 2026-09-13, and it is not a recompiler.** It was
found while preparing the second core, and it matters because it shows the
divergence is not only about CPU backends — the shape of the problem is wider
than the table's first five rows suggested.

`Makefile.libretro` defaults `HAVE_CDROM = 0` at line 7. The `unix` branch then
does this, and no Apple branch does anything equivalent:

```make
ifneq ($(findstring Linux,$(shell uname -s)),)
  HAVE_CDROM = 1
endif
```

which reaches the compiler as `-DHAVE_CDROM`. It is the libretro **physical
CD-ROM drive** interface, and it lands on Sega CD — the one platform of the four
this core serves that the handover already flagged as saving by a different
mechanism than the other three.

##### MEASURED 2026-09-14: it does not touch the save state format

Both variants were compiled on the test VM and their object files compared.
**Of 115 object files, exactly three differ**, and all three are libretro's
VFS/CD-ROM plumbing:

```
libretro/libretro-common/cdrom/cdrom.o
libretro/libretro-common/vfs/vfs_implementation.o
libretro/libretro-common/vfs/vfs_implementation_cdrom.o
```

**Nothing under `core/` differs.** `core/state.c` is byte-identical, and so is
every `core/cd_hw/*` object — `cdc`, `cdd`, `scd`, `pcm`, `gfx`, `cd_cart`. The
save state is produced entirely by `core/`, so the flag cannot affect it. The
source agrees: `HAVE_CDROM` appears in exactly two files in the whole tree, both
under `libretro/libretro-common/`, and in none under `core/`.

**This is stronger than the cross-load test that was planned**, and it cost no
ROM transfer. A state written by one build and loaded by the other would have
proved that one game's state survives. Comparing the objects proves the entire
emulation core is the same machine code — for all 22 Sega CD titles in the
library, and for the three cartridge systems as well.

**The test is not vacuous.** The standing rule is that a test must first show
the thing it measures actually varies, and it does: the `HAVE_CDROM=1` build is
`b217941923…` against the pinned build's `78a2522871…`. The flag changes the
binary. It just changes none of the binary that matters here.

**The decision stands and the reason narrows.** Keep `HAVE_CDROM=0`, but no
longer out of save-state caution — that is answered. It stays because the
console has no optical drive, so `=1` compiles in three objects of physical-CD
access that can never run, and because matching Cabinet is free. **Stop carrying
the caveat.**

##### FOURTEEN CORES, 2026-09-15 — and what building them taught

Twelve added in one pass, every pin and build argument read from the manifest.
**217 playable games became 934 of 1644.** (Fifteen by the end of that session,
986 games; twenty and 1100 the next day, when the five below landed.)

Three things the pipeline had to learn, each found by building rather than by
reading:

**Upstream output names do not match manifest names, and there is no rule.**
`beetle_ngp` produces `mednafen_ngp_libretro.so`. A hand-maintained table of
twenty-one such names goes stale, and the frontend would need a second copy of
it. So `build-core.sh` discovers whatever `*_libretro.so` the build produced and
files it under the core as the **manifest** knows it — the identity the pins,
the emulator tags and `catalog.cpp` already use. The rename is printed, never
silent. A name already ending in `_libretro` does not get a second one, and
`catalog.cpp` carries the same rule with a comment on both sides saying so.

**Some cores cannot be asked what revision they are, and that is upstream's
bug.** `beetle_pce_fast` and `beetle_saturn` both report a bare version: their
`libretro.c` is a **C** file using `GIT_VERSION`, while their Makefile adds
`-DGIT_VERSION` to **`CXXFLAGS`** only, so the define never arrives and the
empty-string fallback wins. Verified on both rather than assumed from the
matching symptom.

> **Not patched into working.** Adding the missing flag would change our binary
> against Cabinet's, which builds the same upstream with the same blind spot,
> and diverging from Cabinet to satisfy our own test is backwards.
> `VERIFY_REVISION=0` marks such a core with its reason. The *checkout* is still
> asserted at the pinned commit; only reading it back is lost.

**And FBNeo reads archives itself** — its `valid_extensions` are
`zip|7z|cue|ccd`. An arcade set must be handed over **unextracted**, which is
exactly what *ask the core, never the platform* already does. The rule was
written before anything needed it and turned out to be load-bearing on the
first core that did.

##### And the build turns out to be reproducible across machines

The same commit built on a GitHub `ubuntu-24.04` runner and on the Fedora test
VM produced **byte-identical** artifacts:

| | |
|---|---|
| gambatte | `b2ee839c226af409765b083e17b211265a44ca184f5ef85763fe003ad63ca582` |
| genesis_plus_gx | `78a252287120173a8acc65c6d345dbdecb2fd7b0805a28f7df48c05baed3699b` |

Found incidentally while setting up the comparison above, and worth more than it
looks. *"Both machines can build it"* is a weaker claim than *"both machines
produce the same bytes"*. The second means a sha256 in a CI log is a fact about
the revision and the flags rather than about the machine, so a core can be
checked against it anywhere — and it means a future mismatch is a real signal
rather than noise to be explained away.

**Now three, and the third is the one that was in doubt.** PPSSPP, 2026-09-17:
`c93fed82…` on both, from a 38 MB CMake build of roughly four hundred
translation units with a vendored ffmpeg linked in. The two above are small
Makefile builds; this is the shape that could plausibly have picked up a
timestamp or a path. It did not.

**Decision: build with `HAVE_CDROM=0`.** The conservative choice is free here.
The console has no optical drive and never will — ROMs arrive from RomM as
files — so the lever disables a feature the hardware cannot use, and matching
Apple costs nothing to get it.

**The manifest corroborates it.** `build_args` is `null` for `ios`, `tvos` and
`mac`, so nothing overrides the line-7 default on any Apple platform. Verified
at the pinned commit `a7985a9c`, not merely at upstream `master`: the default,
the `uname` test and the absence of any `HAVE_CDROM` in the Apple branches are
all identical there.

**And the manifest records a second divergence of the same shape, in vecx:**
`HAS_GPU=0`, because *"Makefile defaults HAS_GPU=1 off macOS, which builds a
GLES2 path this frontend cannot drive."* Cabinet already passes that on both
Apple platforms. It is a third instance of the pattern — the `unix` branch
asking `uname` what machine it is on and changing the build — and it is the
reason the `build.<platform>` field exists. **Read `build_args` from the
manifest before building any core**, rather than assuming an empty
`MAKEARGS` because the core is not in the recompiler table.

Cabinet's Mac build already pulls several of these levers the other way
(`DYNAREC=ari64`, `JIT_ARCH=aarch64`), so the mechanism is proven; only the
values differ.

**This is why the manifest as previously specified is not enough.** Repository
plus commit SHA does not describe a build. It must also record **the make
arguments and CMake flags**, or two builds of the same revision will differ in
the one dimension that matters.

##### And the source-level patches are part of the build too

**VERIFIED.** `build-core.sh` and `build-flycast.sh` patch upstream source
in-flight, and not all of those patches are Apple workarounds. Three change
behaviour and **must travel to Linux**:

- **Flycast `CPU_RATIO = 2`.** Upstream charges every interpreted SH4
  instruction 8 cycles, an effective 25 MHz, which is the direct cause of heavy
  scenes slowing down *inside the emulated machine*. Cabinet changes it to 2, an
  effective 100 MHz. Both neighbouring values were measured on device and
  rejected. **On a Linux build with the dynarec on, this constant is not used at
  all** — which is a behaviour difference between platforms that nobody has
  reasoned about yet.
- **melonDS's missing unload flush.** Upstream's `retro_unload_game` never
  flushes, so a save made less than two seconds before quitting is dropped —
  and save-then-quit is exactly how people leave a game. Cabinet inserts the
  flush. Linux needs it identically.
- **VeMUlator's `strchr` → `strrchr`.** Finds the extension at the last dot
  rather than the first, so a dot anywhere in a parent directory name does not
  silently load the card as nothing. (VeMUlator is iOS-only, so this one does not
  travel — but it is the same class of patch.)

The Apple-only patches — melonDS's `MAP_JIT` W^X and fastmem bracketing,
pcsx_rearmed's 16 KB page-size fix, Flycast's `JITWriteProtect` dlsym bypass,
the Beetle PCE `zutil.h` `fdopen` fix — **all disappear.**

##### The prefix-and-merge apparatus disappears entirely

**VERIFIED, and confirming what this question guessed.** Cabinet merges each
core into one relocatable object exporting only `<prefix>_retro_*` forwarders,
because Apple's toolchain ships no object-file symbol renamer and only one core
can carry the standard names. On Linux, one `.so` per core `dlopen`ed with
`RTLD_LOCAL` gives that isolation for free.

Gone with it: `bsat_wrapper.c` and its eighteen `sed`-derived copies, the
`ld -r` merges, `-exported_symbols_list`, `ar rcs`, and the `-fno-common`
compiler shim that exists only because Apple's linker cannot localise common
symbols. That is most of `build-core.sh`'s length and nearly all of its
subtlety.

##### The two heavy emulators are the easy half, not the hard half

This document assumed Dolphin and PCSX2 would be the long pole. **They are the
only two things already solved.**

**VERIFIED:** both are **pinned to an exact commit and asserted at build time** —
Dolphin at `a1e636d`, PCSX2 at `c89cb8ae`. `build-dolphin-mac.sh` re-reads
`git rev-parse` and fails if the tree has moved. That is precisely what the core
manifest is supposed to do, already implemented, for the two cases it was assumed
would be hardest.

Their patch scripts classify cleanly:

| | Apple walls that vanish | Seats for the frontend that must be re-cut |
|---|---|---|
| **Dolphin** (8 groups, 19 edits) | Catalyst JIT W^X; libusb `IOServiceAuthorize`; AGL/NSOpenGL; the Quartz input backend; `NSScreen` HDR headroom | where `Sys` lives; audio out; `Pad::GetStatus` input |
| **PCSX2** (17 groups) | Catalyst JIT; CocoaTools AppKit; the Metal renderer's AppKit corners; the Metal present path (3 groups); EyeToy/AVFoundation; the Homebrew/FFmpeg leak; libwebp archive split | the host layer itself; audio; input; on-screen messages; the present guard, in whatever form Vulkan needs |

Roughly **five of Dolphin's eight** and **nine of PCSX2's seventeen** are Apple
or Metal walls that do not exist on Linux. The rest are the frontend claiming
the loop, the audio, the input and the OSD — which it must do on any platform,
and which is *the same work* rather than a port.

And the host layers themselves are already portable: `CabinetDolphinHost.cpp`
(574 lines) and `CabinetPS2Host.cpp` (811), plus their C bridges, are plain C++.
Only the `.mm` files — 155 lines for Dolphin, 681 for PCSX2 — are Apple-specific,
and they are audio, Cocoa shims and the Metal drawable probe.

> **One deliberate divergence from "the same source".** PCSX2 is pinned to the
> **isztldav fork**, which exists solely to add ARM64 JIT recompilers that
> upstream stubs out on Apple Silicon — about 23,000 lines of new arm64 emitter,
> which the fork's own README says was translated with LLM help. **On x86-64
> that fork buys nothing**, and upstream PCSX2's x86-64 recompiler is the
> original, first-class one. CabinetOS should track **upstream PCSX2**, not the
> fork. Record it as an exception in the manifest with this reasoning next to
> it, rather than letting it look like drift.

##### The urgent finding: Cabinet's core build is not reproducible by anyone

**VERIFIED, and this is more serious than the missing SHAs.**

1. `spikes/` is **gitignored**. The per-core source checkouts live only on the
   machine that built the shipping archives.
2. `build-core.sh` derives every core's wrapper by `sed`-ing
   `spikes/BeetleSaturnStatic/bsat_wrapper.c` — a file its own comment describes
   as *"hand-written, not part of the cloned repo."* It is not in the
   repository. **`tools/build-core.sh` cannot run on a fresh clone of Cabinet.**
3. The committed `.a` archives embed no revision. Checked with `strings` across
   several: nothing. The revisions are **not recoverable from the artifacts.**

So the current core revisions exist in exactly one place: the working trees on
one Mac. If that machine is lost, parity cannot be established against what
Cabinet ships today — only re-established from a fresh pin, which invalidates
every save state made so far.

##### RECOVERED, 2026-09-13 — and it found a live bug

The recovery was run on the build Mac the same day. `core-manifest.json` now
exists in Cabinet (not yet pushed at the time of writing). CabinetOS consumes it
once it lands; do not keep a copy here, it would drift.

> **It landed, and this document did not notice for four days.** It is
> `docs/core-manifest.json` in Cabinet, pushed in `37ca75d`, and a fresh clone
> diffs byte-identical against the `~/Downloads` copy this project has been
> reading. Found 2026-09-17 while cloning Cabinet for PPSSPP's build flags.
> **So it is fetchable, and `catalog.cpp`'s table is a candidate for generation
> after all** — that table's own comment says it is hand-written "because the
> manifest is not in this repository and is not fetchable", and half of that
> reason has now expired.

It did not merely record what was there. **It found that Cabinet is shipping
different revisions of the same core to different apps, right now.**

**Eleven of twenty-three cores diverge between iOS and macOS.** In every case
macOS is newer, by between one day and eleven weeks:

| Core | iOS | macOS | Drift |
|---|---|---|---|
| prosystem | 2026-06-04 | 2026-08-22 | **11 weeks** |
| picodrive | 2026-07-29 | 2026-08-20 | 3 weeks |
| beetle_vb | 2026-07-29 | 2026-08-23 | 3.5 weeks |
| fceumm | 2026-07-28 | 2026-08-22 | 3.5 weeks |
| gambatte | 2026-07-31 | 2026-08-21 | 3 weeks |
| beetle_pce_fast | 2026-07-31 | 2026-08-28 | 4 weeks |
| pcsx_rearmed | 2026-08-02 | 2026-08-27 | 3.5 weeks |
| genesis_plus_gx | 2026-08-07 | 2026-08-28 | 3 weeks |
| snes9x | 2026-08-08 | 2026-08-16 | 1 week |
| mame2003_plus | 2026-08-19 | 2026-08-28 | 1 week |
| beetle_saturn | 2026-08-10 | 2026-08-11 | 1 day |

The other twelve are aligned across every platform they ship to: beetle_ngp,
Dolphin, FBNeo, Flycast, GW, melonDS, mGBA, Mupen64Plus, Opera, PCSX2,
PPSSPP, Stella2014, vecx, VeMUlator.

The cause is visible in `build-core.sh` and is exactly the mechanism this
document predicted: a separate checkout per platform, each cloned `--depth 1`
whenever that platform was *first* built. macOS support landed most recently, so
its trees are the freshest.

**And eleven of the twenty-one tvOS revisions are gone.** Those per-platform
checkouts no longer exist on the machine, and the archives embed nothing:
beetle_ngp, beetle_pce_fast, beetle_saturn, fceumm, gambatte, genesis_plus_gx,
mGBA, pcsx_rearmed, picodrive, prosystem, snes9x.

Put those two facts together and the live consequence is this:

> **For eleven cores, the compatibility of a save state between Cabinet's Mac
> and its Apple TV is unknown today and cannot be made known**, because the
> revision one side was built from no longer exists anywhere.

##### CORRECTION, 2026-09-16: two of the eleven were recovered from the archives

"The archives embed nothing" was checked with `strings` across several cores and
it is not true of all of them. It is not true of the two that were looked at
today, and both were on the unrecoverable list:

| | |
|---|---|
| `libpicodrive_tvos.a` | `2.05-733c711` — **the pinned revision**, and the same one the Mac ships |
| `libmgba_tvos.a` | `e31759b24e7a4e3899285ff720d7b573ac328ae7`, in full |

So tvOS picodrive was never behind; only iOS was, at `6248b51`. The manifest
records picodrive as diverging across platforms with tvOS unknown, and the
answer was in the binary the whole time.

**The mechanism is the one this document found later and did not go back and
apply**: a core that compiles `git rev-parse --short HEAD` into its version
string carries that string into the archive. The recovery ran before that was
understood, which is why it concluded unrecoverable.

> **Worth an hour, Cabinet-side: run `strings` over the other nine tvOS
> archives.** Every Makefile-based core in the set has the same `GIT_VERSION`
> line, so several more revisions are probably sitting in the artifacts. Each
> one recovered turns "unknown and unknowable" into a fact, and shrinks the
> regression surface the realignment release has to carry.

##### And two cores lost their revision on Cabinet's side, in opposite ways

Both found by comparing our builds against the shipping archives:

- **mGBA's Mac archive reports `e31759b24-dirty`.** Cabinet's Mac build carries
  a working-tree modification that no script applies — the same class of problem
  as Flycast's unscripted edits, in a core whose manifest entry lists no patches
  at all. The iOS and tvOS archives are clean at that commit.
- **melonDS's archives report `melonDS 0.9.3` with no revision**, while the same
  upstream built here reports `0.9.3 66b5d26`. Its Makefile has the
  `GIT_VERSION` line and it reached our binary, so something about Cabinet's
  build is losing it — the same failure `build-core.sh` was taught to prevent by
  passing `safe.directory` through the environment.

##### What the manifest decided, and the cost it commits to

`core-manifest.json` chooses a `pinned_commit` per core — the macOS revision
wherever there was a choice, on the grounds that it is the newer of the two in
use and has been play-tested there. **That is the right call**, and it makes the
lost tvOS revisions stop mattering, because they are being replaced rather than
matched.

It is not free, and the cost should be taken deliberately rather than
discovered:

- **Eleven cores get rebuilt for iOS and tvOS at revisions never run on those
  platforms.** That is a real regression surface arriving all at once. It wants
  its own release, not a change mixed in with others, and it wants
  `docs/core-quality-pass` re-run afterwards.
- **Existing Apple TV and iPhone save states for those eleven cores may not
  survive it**, and nobody can check in advance. That is acceptable *now* —
  alpha, one user — and will not be acceptable once there are people with save
  histories. **This is the last cheap moment to do it.**

##### The hole that is still open, and it is in the worst possible core

The manifest records, in Flycast's own patch entry:

> `"where": "tools/build-flycast.sh, and UNSCRIPTED edits in the working tree"`

**So the pin does not reproduce the shipping Flycast.** Commit `a172e000` plus
`build-flycast.sh` gives something, but not what is in the app, because there
are hand edits in that tree that no script applies.

Flycast is the worst core for this to be true of. It is Dreamcast *and* Naomi,
it is the heaviest system that is not PS2 or GameCube, and it is one of only two
cores that can answer the save-state parity question cleanly.

The Flycast patch inventory is also the thinnest in the manifest — one vague
entry covering `CPU_RATIO=2` *"plus recompiler/W^X and SH4 changes"*, with
`build-flycast.sh`'s own `first_run` and VMU-screen-callback patches not listed
separately at all.

**Before anything else touches that tree:**

```
git -C spikes/cores/flycast/src diff > tools/patches/flycast-unscripted.patch
git -C spikes/cores/flycast/src status --porcelain
```

Commit the patch file, then either fold it into `build-flycast.sh` or apply it
from the file. Until that exists, Flycast cannot be rebuilt — on any platform,
including the ones that ship today.

##### And `bsat_wrapper.c` is still not in the repository

`build-core.sh` derives every core's wrapper by `sed`-ing
`spikes/BeetleSaturnStatic/bsat_wrapper.c`, which is gitignored and
hand-written. `build-flycast.sh` carries a complete equivalent inline as a
heredoc, so this is a copy rather than a rewrite — but until it is done,
`build-core.sh` cannot run on a fresh clone.

##### BUILT IN CI, 2026-09-13 — and the build-time assertion now exists

`.github/workflows/build-core.yml` builds the core matrix on a GitHub runner,
in the same Fedora 44 builder container the frontend and the test VM use. The
first green run took about 45 seconds from a bare checkout.

More importantly, the assertion this question asks for — *"asserts at build
time that what it produced matches"* — is implemented, and it checks the
artifact rather than the checkout:

> `build-core.sh` asserted the **checkout** was at the pinned commit. That
> proves what went in, not what came out.

Every Makefile-based core in the set compiles its own revision into the string
it reports through `retro_get_system_info`:

```
GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
```

`tools/core-info.c` reads that back out of the finished `.so`, and fails the
build if it is not the pinned revision. It `dlopen`s with `RTLD_LOCAL` the way
the frontend does, and checks the libretro API version against the header the
frontend was compiled against. **No ROM is needed** — `retro_get_system_info`
is documented as callable before `retro_init`, which is the only reason any of
this is runnable in CI, where there is no game to give it.

What gambatte reports, from the first CI run:

```
name        Gambatte
version     v0.5.0-netlink d9d6cd0
extensions  gb|gbc|dmg
api         1
revision    d9d6cd0, as pinned
sha256      b2ee839c226af409765b083e17b211265a44ca184f5ef85763fe003ad63ca582
```

**The `-netlink` suffix was checked, not assumed.** A Linux build quietly
turning on what the Apple build has off is the entire subject of this question,
so `HAVE_NETWORK` was read from the Makefile: it is set in the `unix` branch and
in both the `ios-arm64` and `tvos-arm64` branches. All three agree. This is
parity holding, not drift.

**One live failure mode was found and closed while writing the assertion.** Git
refuses a bind-mounted tree it considers dubiously owned; the core's own
`|| echo unknown` swallows that silently; and the result is a core that does not
know what revision it is — which is exactly the fact this question is about.
`build-core.sh` now passes `safe.directory` in through `GIT_CONFIG_*` rather
than writing a gitconfig into the mounted tree.

**What CI still cannot answer.** Save-state parity needs a ROM, and no ROM
belongs in this repository. `tools/state-probe.c` remains the instrument and the
test machine remains where it runs.

#### The core manifest, revised

Supersedes the sketch above. Per core:

| Field | Why |
|---|---|
| `repo` | upstream URL |
| `commit` | exact SHA, **checked out and asserted**, never `--depth 1` of `HEAD` |
| `systems` | which RomM platforms it serves |
| `platforms` | which targets it is built for |
| `build.<platform>` | **the make arguments or CMake flags**, per platform — this is the new field, and the table above is why |
| `patches` | which in-flight source patches apply, and to which platforms |
| `notes` | deliberate divergences, like PCSX2's fork |

It belongs in **Cabinet**, because Cabinet is the app that ships and the source
of the constraint. CabinetOS reads it, builds the same revisions with the
recorded flags, and asserts at build time. A mismatch becomes a failed build
rather than a save state that silently will not load.

#### ANSWERED 2026-09-15, for pcsx_rearmed: the CPU backend does not break states

**The question this section was written to pose.** Read from the source at the
pinned commit `ba61a4fd`, after an object-file comparison of `DYNAREC=0` against
`DYNAREC=lightrec` showed `libpcsxcore/misc.o` differing — which is where
`SaveState` and `LoadState` live, and looked at first like the bad answer.

It is not. `SaveState` and `LoadState` both call `ndrc_freeze`, and that
function is built to be read by a core with a different backend from the one
that wrote it:

- **Saving with no recompiler blocks writes nothing.** `new_dynarec_save_blocks`
  returns 0 and `ndrc_freeze` returns before writing a byte.
- **Loading tolerates the section being absent**: the 8-byte `"ariblks"` header
  fails to match, the reader seeks back, and the state continues to parse.
- **Loading tolerates it being present and useless**: the size is read, the data
  is consumed, and then `if (psxCpu != &psxInt) new_dynarec_load_blocks(...)`
  declines to apply it on an interpreter.

And what the section holds is **block addresses** — a recompiler cache hint, not
emulated machine state. That is why it is optional at all.

**A lightrec build writes no section either.** The real implementation is gated
`#if !defined(DRC_DISABLE) && !defined(LIGHTREC)`, so LIGHTREC takes the same
stubs the interpreter does.

##### What this settles

> **Cabinet's single `pcsx-rearmed-native` tag across `DYNAREC=0` on iOS/tvOS
> and `DYNAREC=ari64` on the Mac is correct, not a latent bug.** It had looked
> like one: the same tag on two different CPU backends is exactly the
> configuration this document warned could silently produce unloadable states.

**So CabinetOS can take the faster Linux backend and still share the tag.** On
Vega integrated graphics that is the difference between comfortable and
marginal for PS1, and it was the thing this question was holding back.

**Scope, stated precisely.** This is pcsx_rearmed, read from its source rather
than measured by cross-loading a state. melonDS, picodrive, Flycast and
mupen64plus each need the same check before their tags are shared — a different
core may put real machine state behind its dynarec, and nothing here says
otherwise. The object-file diff is the way in: it took two minutes and pointed
straight at the one file worth reading.

#### ANSWERED 2026-09-16, for melonDS and picodrive — and this time it was measured

The scope note above asked for the same check on four more cores. Two of them
are now done, and done better than pcsx_rearmed was: not read from the source
and believed, but **run**, with each build loading the other's state and each
build loading its own as the control.

`cores/backend-diff.sh` builds a core twice, changing one variable, and reports
which objects differ. `cores/hash-objects.py` is what makes that answer true.
Both are new, and between them they are the instrument this question needed.

##### melonDS: take the recompiler, share the tag

**The lever.** The unix branch sets `JIT_ARCH=x64` on x86-64; Cabinet's iOS and
tvOS builds set nothing and run the interpreter, its Mac sets `aarch64`. Same
shape as pcsx_rearmed, and Cabinet again ships both under one tag.

**The object diff** put the difference in the machine, not beside it: ten
objects changed, including `NDS.o`, `DSi.o`, `ARM.o` and `CP15.o`, plus twelve
that exist only in the recompiler build. That is a much larger footprint than
pcsx_rearmed's single `misc.o`, and it is why reading was not enough here.

**The source says it is deliberate.** In the whole core there are exactly two
`#ifdef JIT_ENABLED` blocks inside any `DoSavestate`, both guarded
`if (!file->Saving)`, and neither writes a byte. Nothing about the recompiler is
ever stored. On LOAD a JIT build repairs what an interpreter state does not
carry and throws its block cache away:

```c
// hack, the JIT doesn't really pipeline
// but we still want JIT save states to be
// loaded while running the interpreter
FillPipeline();
```

and, at the end of `NDS::DoSavestate`, `ARMJIT::ResetBlockCache()` and
`ARMJIT_Memory::Reset()`. The interpreter build even keeps the `JIT_Enable`
variable, with upstream's comment "Needed for savestate".

**And then it was run**, against Tetris DS through `tools/state-probe.c`:

| | |
|---|---|
| 600 frames from boot, interpreter | video `f359e84a8fed0383`  audio `675a983465de49b3` |
| 600 frames from boot, recompiler | video `f359e84a8fed0383`  audio `675a983465de49b3` |
| state size, both | 6,526,677 bytes |

Every load combination — each build's own state and the other's — ran on to the
same digest, `41b5c97d81c50383`, with the two own-state runs as the control.

**The trap that would have made this prove nothing**, and it is this document's
own: an unanswered core option. `state-probe` answers `GET_VARIABLE` with NULL,
so `melonds_jit_enable` never reaches the core, and if `Config::JIT_Enable` had
then been zero the "recompiler" build would have run the interpreter and the two
sides would have been identical for the most boring possible reason. It is
`int JIT_Enable = true` under `#ifdef JIT_ENABLED` — a C++ initialiser rather
than a zeroed C global — and the run confirms it, printing "Resetting JIT block
cache" on that side only.

> **CabinetOS builds melonDS with `JIT_ARCH=x64` and writes `melonds-native`.**

##### picodrive: the states are identical, and we take the interpreter anyway

**The lever** is `use_sh2drc`, the SH2 recompiler the 32X needs two of. It
defaults to 1 on x86-64; Cabinet gets 0 from the Makefile's own Apple block,
turned off there for code-signing reasons that do not apply to this console.

**The states are byte-identical.** Both builds run to frame 600 and write states
that `cmp` reports as not differing at all. The source agrees: `sh2_pack` copies
`SH2_REG_SIZE` bytes, which is `offsetof(SH2, macl) + sizeof(macl)`, and every
drc field in the struct sits after `macl`. `SH2_STATE_SIZE` is a compile-time
constant either way.

**But the two backends do not produce the same picture.** From an identical
boot they diverge in video and audio digest within 60 frames while converging on
that identical machine state — something timing-visible lands differently. Each
build is deterministic on its own (same digests twice), so it is the backend.

So the choice is not about states at all, and it comes down to this: nothing
here needs the recompiler. The 32X is two 23 MHz SH2s and this is an x86-64
console. Taking it would buy performance nobody is short of and pay for it with
a picture that differs from the Apple TV's.

> **CabinetOS builds picodrive with `use_sh2drc=0` — exact flag parity with
> Cabinet — and writes `picodrive-native`.** Revisit only with a measurement
> from real hardware, knowing the states will survive the change.

##### The instrument itself needed fixing first, and that is the lesson

The picodrive comparison first reported **102 of 103 objects differing,
including zlib's** — which no CPU backend can reach. Read as a result, that
number costs this core its shared tag.

It was not a result. A control run with **the same setting on both sides**
reported the same thing, and the cause is that picodrive builds with `-flto`:
GCC writes a random per-invocation id into every LTO section name.

```
.gnu.lto_.profile.3bda9114828bb356        first build
.gnu.lto_.profile.ca2804fcf73ae283        second build
```

`cores/hash-objects.py` replaces that id with sixteen zeroes — length
preserving, so nothing in the file moves — before hashing. That took the noise
from 102 objects to about 12, and the remaining 12 are a different set each
time.

**The deeper point is that on an LTO core the objects are not the emulator.**
The machine is generated at link time, so the intermediate objects hold compiler
bytecode and only the artifact is meaningfully reproducible. And it is:
**four builds of picodrive produced four byte-identical `.so` files** while
differing in a random handful of objects each time. `backend-diff.sh` now says
so itself and points at `state-probe` instead when it sees that pattern.

Two things follow for the rest of this document:

1. **"The build is reproducible" is a per-core claim.** It was established for
   gambatte and genesis_plus_gx, neither of which uses LTO. picodrive is
   reproducible where it counts and not at the object level, and a future core
   may be neither.
2. **Run the control first.** `backend-diff.sh` takes the same setting twice and
   reports identical-is-a-pass, which costs one pair of builds and is the
   difference between a measurement and a number.

##### mupen64plus: it builds, and the tag stays unshared

This core has **four** levers rather than one — `WITH_DYNAREC`, `FORCE_GLES3`,
and the `LLE` / `HAVE_PARALLEL_RSP` / `HAVE_PARALLEL_RDP` / `HAVE_THR_AL` group
that selects low-level RSP and RDP emulation. Cabinet turns the last four on and
the unix branch leaves them off, so "match the CPU backend" was never the whole
job here.

**And matching Cabinet's backend does not link.** With `WITH_DYNAREC=` empty,
`cp0.c`, `interrupt.c` and `r4300_core.c` still reference `dyna_jump`,
`dyna_stop` and `dynarec_jump_to`, because those calls are guarded by
`#ifndef NO_ASM` rather than by `WITH_DYNAREC`. Cabinet's ios-arm64 case adds
`-DNO_ASM`; the unix case has no equivalent, so the .so fails to link with five
undefined references. `DYNAFLAGS` is the only variable that can carry the define
in from the command line without replacing a flags variable wholesale.

**One difference is recorded rather than matched.** Cabinet also adds
`-Ofast -funsafe-math-optimizations` to three flags variables that cannot be
extended from the command line, so this build gets the unix branch's
`-O3 -ffast-math`. That is a floating-point difference in an emulator whose
output is floating point.

> **No emulator tag for mupen64plus.** It cannot be settled while the core
> cannot run here, and an unshared tag costs nothing today because nothing can
> write an N64 state yet.

##### And running one of them found a save bug that building them could not

melonDS is the first core CabinetOS ships that writes its own save file rather
than exposing `RETRO_MEMORY_SAVE_RAM`. Launching Tetris DS from the real library
printed:

```
Save file: /Tetris DS.sav
```

At the **root of the filesystem**, where it cannot be written. The frontend was
passing its save directory to `loadGame`, and melonDS reads the directory in
`retro_init` — inside `Core::load`, which happens first — copies it into a
static buffer and never asks again. It had been handed an empty string.

**The failure is silent.** The game runs, the save never lands, and nothing says
so. And it is not a melonDS bug: it hits every core that writes its own save
file, which is the class Cabinet already lost saves to once by a different
route — Neo Geo Pocket, Sega CD, FBNeo's NVRAM, Dreamcast's VMU. Two of those
have been "playable" on this console for days.

`Core::setDirectories` now exists and is called **before** `load`, at both call
sites. Verified by running it again: `romcache/saves/Tetris DS.sav`.

> **This is the argument for launching a core rather than building it.** Every
> assertion in the build pipeline passed on that core — pinned commit, asserted
> revision, reproducible artifact — and none of them could see this.

**Still open, and now concrete:** the sync layer only knows about
`RETRO_MEMORY_SAVE_RAM`, so `[save] battery is 0 bytes` is correct for melonDS
and the `.sav` beside the ROM is not uploaded to RomM at all. Phase 4 owes the
file-writing class its own path, the way `MemoryCardSync` does in Cabinet.

##### A core that is built and still cannot be run is a third thing

Building Flycast and Mupen64Plus would have made `catalog::coverageFor` call
Dreamcast and N64 **Playable**, because its installed-check is "is the .so on
disk". They are not: both render through `RETRO_ENVIRONMENT_SET_HW_RENDER`,
which `core.cpp` refuses. That is the hero-offering-an-arcade-game bug again,
one layer further in.

So `Support` now carries **`NeedsHardwareRender`** beside `NoCore`, `Excluded`
and `NotInstalled`. Four answers, and they lead to four different pieces of
work — which is the whole reason this document warned against collapsing them.

**RESOLVED 2026-09-16, and the fourth answer is now empty.** The host owns the
GLES context and hands a hardware-rendered core a framebuffer inside it, so
Dreamcast, Naomi and N64 are Playable because they play — Mario Kart 64 and
Ikaruga were launched from the real library and photographed running. See
*Video: two paths* for what the mechanism actually turned out to require.

`NeedsHardwareRender` is kept rather than deleted, because it is still the
honest answer for the narrower case it was always really about: a core that
wants **desktop GL or Vulkan**, which this context is not. Nothing answers it
today. PPSSPP is the one core left unbuilt and is the next one to find out
about — and `catalog.cpp`'s `hwRender` flag is kept in place for it, also
because that table is positional and removing a field silently re-assigns every
row below it.

**What running them found that building them could not**, again:

- **Flycast writes its Dreamcast flash to the SYSTEM directory**, not the save
  directory — `system/dc/dc_nvmem.bin` — and Mupen64Plus writes a 447 KB
  `system/Mupen64plus/mupen64plus.ini`. Neither is a ROM, so neither is visible
  to cache eviction. See *Measured behaviour* for the whole of that gap.
- **Ikaruga opens on "no memory card connected"**, which is the file-writing
  save class showing its face rather than a fault: Flycast exposes no
  `RETRO_MEMORY_SAVE_RAM`, `[save] battery is 0 bytes` is correct, and the VMU
  is a file the sync layer does not yet know about.
- **Save states work on both.** Flycast serialises 35.9 MB and restores to an
  identical video digest; the test's own guard reports the scene as static at
  that point, so it proves the round trip rather than a long divergence.

#### PPSSPP, 2026-09-17: the twenty-first core, and its lever is not a build flag

**Built, run, and playing.** Lumines reaches its attract demo in colour with
sound, writes its memory-stick save, and quits back to Home with the shelf
showing it as recently played. Hammerin' Hero does the same. That is PSP, and it
finishes the set at **twenty-one of twenty-one**, 1147 of 1644 games.

| | |
|---|---|
| Pinned commit | `c989c2553e1099730736d965c221823fe974fa55` — the same one on every platform Cabinet ships it to |
| Reports | `PPSSPP v1.20.4-1359-gc989c2553`, asserted against the pin |
| Content | 480x270 into a 480x272 target, 59.9401 fps, 44100 Hz, aspect 1.7778 |
| Context | **OpenGL ES 2.0**, bottom-left origin — the only core in the set that asks for ES 2 |
| Save state | 41,943,040 bytes at the demo screen |
| System files | 13 MB, 43 files, installed beside the core rather than fetched from RomM |
| sha256 | `c93fed82…` — **the same on the test VM and on a GitHub runner** |

**It is reproducible across machines, and that is a third data point rather than
a repeat.** gambatte and genesis_plus_gx are small Makefile builds; this is a
38 MB CMake build of roughly four hundred translation units with a vendored
ffmpeg linked in, produced from a bare checkout in CI in 8m52s, and the two
artifacts are byte-identical. A sha256 in a CI log is a fact about the revision
and the flags rather than about the machine — see *And the build turns out to be
reproducible across machines*, which said that when it had two cores to say it
about.

##### The CPU backend is an OPTION here, not a build flag

Five cores in this set pull their backend lever in `cores/build-core.sh`.
PPSSPP does not have one: it chooses between three CPU engines **at runtime**,
from `ppsspp_cpu_core`, whose declared default is `JIT` — the native x86-64
recompiler. Cabinet ships `IR JIT`, upstream's own string for the IR
interpreter, on all three of its platforms.

**CabinetOS matches Cabinet, and the lever moved to `catalog::optionOverrides`,
which now has its first entry.** The standing rule applies unchanged — match
Cabinet's configuration until a backend difference has been measured not to move
the state format — and nothing is given up by obeying it: PSP is four games, and
Cabinet's own bench found the IR interpreter *faster* than the recompiler on an
M4 (Lumines 1.93 ms mean against 3.05) because compilation stalls land inside
frames. What it costs, plainly: on an x86-64 console the native recompiler is
the engine PPSSPP is usually run with, and this leaves it off.

##### And it will not tell you which engine it picked, so it was made to

`MIPSState::Init` turns `cpuCore` into one of three very different objects and
says nothing, and the libretro layer will silently rewrite a request for the
recompiler into the IR interpreter. In Cabinet that left the question of which
engine was running unresolved for days. Cabinet's answer is a one-line log in
its **Mac** build; this build carries the same line on the only platform it has,
at WARN rather than INFO because that is this host's log floor.

It earns its place immediately. The core now says, on every boot:

```
[core] [CPU] cabinet: CPU engine = 2 (0 interpreter, 1 native JIT, 2 IR interpreter, 3 JIT+IR)
```

and with `--core-options-off`, the control, it says **0** — the plain
interpreter, because an unanswered `ppsspp_cpu_core` leaves
`g_Config.iCpuCore` at the `CPUCore::INTERPRETER` that `retro_load_game` sets
before it reads any variable. **That is this document's own central rule
demonstrated in a third core, from the core's own mouth.**

##### And that control run does something better than make a point: it does not start

With every option unanswered, the same launch ends at

```
[launch] the core needs a render target this context cannot build
```

because `ppsspp_internal_resolution` is then the internal default of 0, *"Auto
(native)"*, which sizes the render to a display a libretro frontend never
reports — so the declared geometry comes back **0x0** and the host refuses to
build a target for it.

**Without the core-options work of 2026-09-16, PSP would not run on this console
at all.** Not "run badly", not "run silently wrong": not start. Cabinet found the
same trap on its first PPSSPP boot and had to answer the option by hand;
CabinetOS gets it for free from answering every declared default, which is the
first time that work has paid for itself in a way that can be pointed at. And
the host fails loudly with the reason rather than showing a black screen, which
is the other half of the same design.

##### Two CMake levers, and one of them decides whether the core runs here at all

`build_args` is null for every platform in the manifest, which is not what the
builder actually passes — see the Cabinet-side note below. Read from
`tools/build-ppsspp.sh` instead:

- **`USING_GLES2`.** `LibretroGLContext` asks for `RETRO_HW_CONTEXT_OPENGLES2`
  when it is defined and `RETRO_HW_CONTEXT_OPENGL` when it is not. This
  frontend's context is EGL/GLES and refuses desktop GL **by name** — so without
  this flag the twenty-first core would have been the first customer for
  `Support::NeedsHardwareRender`, exactly as this document predicted it might
  be. Cabinet gets the flag from upstream's iOS toolchain; the unix build has no
  equivalent and would quietly ask for desktop GL.
- **`MOBILE_DEVICE`.** Cabinet gets this from the same toolchain, and `LIBRETRO`
  does not imply it — only `ANDROID` does. **Read rather than assumed:** every
  use of it in the tree is AVI/WAV dumping, window geometry, the keymap or the
  desktop UI, and the three sites in `Core/SaveState.cpp` are dump-restart
  bookkeeping around a save rather than state content. It is not free, though —
  the `Core/Config.cpp` block it disables also carries `AnisotropyLevel`'s
  default of 4, so leaving it off would change texture filtering against the
  Apple TV's picture for no reason.

FFmpeg comes from the vendored `ffmpeg/linux/x86_64` prebuilt archives, which is
the same mechanism Cabinet uses with `ios/universal` and `tvos/arm64`.

##### The firmware special case, finally concrete

PSP's "firmware" is not a console's and does not come from RomM. It is fonts,
VFPU lookup tables and a per-game compatibility list that ship **with the
emulator** — in the app bundle on Apple, and here as files `build-core.sh`
installs into the frontend's system directory, where `retro_init` appends
`PPSSPP` and warns *"Core system files missing, expect bugs"* if `compat.ini` is
not there.

The set is Cabinet's 43 files rather than upstream's whole 22 MB `assets/`
directory: the difference is the desktop UI's — the web debugger, themes, UI
images, sound effects, the SDL controller database — and Cabinet's subset is the
one that has actually run PSP games on a television.

**Where they live in the image is DECIDED as of open question 18 and BUILT as
of 2026-09-19: `/usr/share/cabinetos/system/`**, which `storage::ensureTree`
symlinks into the console's system directory at startup. A path in `/usr` is
right because the core only ever reads it, and it takes 13 MB of build output
out of a directory that otherwise holds the person's own files.

`build_files/install-frontend.sh` puts them there, which is Phase 5's deploy —
it could never have been done in `build_files/build.sh` alone, because the
files come out of a core build and until that day the image carried neither the
cores nor the frontend. The build still stages them at `cores/system/PPSSPP`,
so **on the test VM they sit in `bios/PPSSPP/` where the core build left them
and the link step correctly leaves them alone**, which is the development case
the link step was written to tolerate.

##### The emulator tag IS shared, and this is the strongest case in the set

`ppsspp-native`. There is no configuration difference left to justify: the
commit is identical on every platform Cabinet ships it to, both of the patches
Cabinet's builder applies travel and are asserted, both CMake levers are
matched, and the CPU engine is answered with Cabinet's own value. What is not
proved — and this is equally true of the five tags that came before it — is that
a state written by this build has been loaded by Cabinet's. The cross-platform
load was proved once, on gambatte; every tag since rests on configuration parity.

##### PSP save DATA does sync, and this document said the opposite for a day

**Corrected 2026-09-17, by MMagTech, who said "I have other games for it with
their memory card in RomM."** He is right, and it took one API call to confirm:

```
Lumines - Puzzle Fusion (USA) (Cabinet).srm   51,426 bytes
emulator = ppsspp-native      updated 2026-08-28
users/…/saves/Playstation Portable/967/ppsspp-native/
```

**How the wrong claim was arrived at, because the mistake is reusable.**
`NativeCore.savesOverSaveRAM` excludes `.psp`, and the comment above the
exclusion says in as many words *"Save sync for PSP is its own future
feature."* That comment was read and believed. It is **stale in Cabinet's own
source**: the feature was built afterwards, four files away, and nothing went
back to update the comment. A `grep` for `archivePSPSaveData` in the same
directory would have shown three call sites in under a second.

> The rule this project already has is *look at the machine rather than
> reasoning from a message*. A source comment is a message. It deserves exactly
> the same suspicion as an error string, and it aged worse than the code did.

**What Cabinet actually does**, read from `MemoryCardSync.swift:324` and
`NativeLauncher.swift:212-260`:

- PSP is a **tree rather than a file**, so `archivePSPSaveData` serialises the
  whole `PSP/SAVEDATA` subtree into one blob with `FileWrapper`.
- It is captured on **the same trigger as every other platform** — after the
  core has shut down and flushed — compared against the previous local
  snapshot, **written locally first**, and only then uploaded.
- It rides the **same store, the same `/api/saves` endpoint and the same
  `saveRAM` region** as a cartridge battery. There is no separate PSP path in
  the sync layer at all; the difference is entirely in how the bytes are
  gathered.
- Only `PSP/SAVEDATA` travels. NAND, `PPSSPP_STATE` and `SYSTEM/CACHE` sit
  beside it and are this device's own machine state, save states and compiled
  shaders — *"uploading them would put tens of megabytes of nothing on the
  server and mean nothing on the other end."*
- The restore is deliberately additive: a save folder the archive does not
  contain is never removed, so a wrong newest-wins costs a stale slot rather
  than somebody's save.

##### So the real obstacle is the FORMAT, and it is much narrower than "no design"

`FileWrapper.serializedRepresentation` is Apple's serialised-directory archive.
Read off the actual bytes rather than assumed:

```
00000000: 7274 6664 0000 0000 0300 0000 0400 0000  rtfd............
00000010: 1700 0000 5f5f 4055 5446 3850 7265 6665  ....__@UTF8Prefe
```

`rtfd`, then little-endian length-prefixed names — `__@UTF8PreferredName@__`,
`ULUS10002LUMINES`, `SAVEDATA` — and the payloads stored **uncompressed and
contiguous**: `PARAM.SFO`'s ` PSF` at offset 230, two PNGs at 5150 and 21404
with their `IEND` markers where they should be.

**CabinetOS has no Foundation**, so it cannot call `FileWrapper`. But it does
not need to reverse-engineer anything either — the container is a flat
length-prefixed table with uncompressed members, which is a bounded afternoon's
parser, and **there is a real 51 KB save on the server to check the round trip
against.** That is a far better position than the "future feature" this section
previously claimed.

##### What PPSSPP is SUPPOSED to use, which is the question that settles it

**MMagTech, immediately after the correction above: "we're using PPSSPP, what
format is it supposed to use?"** That is the right question and it has a plain
answer.

**PPSSPP's save format is the FOLDER.** `PSP/SAVEDATA/<GAMEID><TITLE>/`, holding
`PARAM.SFO`, `DATA.BIN` and the icons — which is what a real memory stick holds
and what PPSSPP reads and writes on every platform it ships on. Verified
first-hand rather than from a page: Lumines wrote exactly
`PSP/SAVEDATA/ULUS10002LUMINES/{PARAM.SFO, DATA.BIN, ICON0.PNG, PIC1.PNG}`.

**There is no single-file PSP save format, and nobody upstream defines one.**

- **PPSSPP** documents where the memory stick lives and defines no export or
  archive format at all.
- **RomM** does not specify one either. Its saves page lists per-platform
  extensions — `.srm`, `.sav`, `.eep`, `.fla` — and says nothing about PSP or
  about directory saves, and the device-sync protocol treats a save as an
  opaque named file with an mtime and a SHA1.

**So the container is a free choice, and `.srm` was the wrong label.** Cabinet
picked `FileWrapper`, which is Apple's `rtfd` directory archive, and named the
upload `.srm`, which is a SNES and Genesis save-RAM extension. The file on the
server is neither an `.srm` nor readable anywhere without Foundation.

**The de facto answer in the PSP world is a plain zip of the save folder.** That
is how PSP save data is distributed and how a person installs one into PPSSPP —
extract the folder into `PSP/SAVEDATA`. It is also free here: the frontend
already links **libarchive** for ROMs, which writes zip as well as reads it.

> **This reverses the recommendation made an hour earlier in this section.** It
> said keep `rtfd` for compatibility. That was the wrong trade once the actual
> quantity was known: there is **exactly one** PSP save on the server. Migrating
> one file is a one-off; keeping `rtfd` means this console carries a
> hand-written parser for an undocumented Apple format forever, and so does
> anything else that ever touches these saves — Grout, a handheld, RomM's own
> web UI.

##### DECIDED: Cabinet moves. Fixed there 2026-09-17, not yet pushed

**MMagTech: "we fixed it on cabinet just hasn't been pushed to github."** So the
question of who moves is answered — CabinetOS targets **zip** and does not need
to write `rtfd`.

**Recorded as reported, not as verified.** The Cabinet source is not on this Mac
(checked: `~/Documents/Cabinet` is the ROM and BIOS folder, and the only git
repo here with a Cabinet remote is this one), and the change is not on GitHub
yet, so nothing here has seen it.

**It can be verified without the source, from one uploaded save.** As of
2026-09-17 the server still holds only the August file, first bytes `rtfd`. The
moment a PSP game is played on the fixed build, four bytes settle it —
`PK\x03\x04` is zip.

**Three things to read off that first upload, because each one silently breaks
the other end:**

1. **What the zip is ROOTED at.** `ULUS10002LUMINES/PARAM.SFO`, or
   `SAVEDATA/ULUS10002LUMINES/…`, or `PSP/SAVEDATA/…`? Any of them is fine and
   they are not interchangeable: unzip to the wrong level and the files land
   one directory off and the game simply does not see the save. This is the
   detail most likely to be got wrong on the second end, and it is invisible
   until someone looks inside the file.
2. **Whether Cabinet still READS `rtfd`.** This one risks MMagTech's own data
   rather than ours: the August Lumines save is the only PSP save he has, it is
   still `rtfd`, and if the fixed build dropped the old read path then that save
   is now unreadable on his own devices.
3. **Whether the tag is still `ppsspp-native`.** It is what this console writes.
   If Cabinet moved it, saves and states stop lining up between the two.

Either way the sync layer itself needs nothing new: same store, same endpoint,
same tag, same after-shutdown trigger.

**Zip is measured, not assumed.** The round trip was run on this console against
the real save: zip the folder, delete the original, unzip it back, and all four
files return byte-identical (`PARAM.SFO`, `DATA.BIN`, `ICON0.PNG`, `PIC1.PNG`).
Lumines then launched against the restored folder, ran and quit to Home with no
file errors, and the files were still identical afterwards. **PPSSPP is never
handed the zip** — it reads loose files from `PSP/SAVEDATA/<GAMEID><TITLE>/`, as
it always has, and the container exists only between the app and RomM. The zip
is also smaller than the Apple archive, 40,626 bytes against 51,426, because it
deflates and `rtfd` does not.

*Not proved*: that the game displayed a "continue" option, which needs a
controller and an in-game menu. What is proved is that the bytes the core reads
are bit-identical to the bytes it wrote.

Verified by running: Lumines wrote all four files under
`romcache/saves/PSP/SAVEDATA/ULUS10002LUMINES/`, so what CabinetOS has to
archive is exactly what Cabinet archives.

##### What running it found that building it could not, for the fourth time

Every assertion in the build pipeline passed — pinned commit, asserted revision,
reproducible artifact — and none of them could see any of this.

1. **A relative save directory is not a path to every core.** The host named its
   directories relative to where it runs. PPSSPP wraps them in a path type that
   asks whether a path is absolute and behaves differently when it is not, and
   mounted the memory stick somewhere it could not write: the game ran, the save
   failed, and the only sign was the core's own `Error writing file
   ms0:/PSP/SAVEDATA/...`. **Cores are handed absolute directories now, and the
   directories are created before the core is told about them.** That is the
   melonDS lesson arriving a second time by a different route, so it is fixed
   once in the host rather than per core.
2. **`need_fullpath` meant the ROM was read into memory and then ignored.**
   Twelve of the twenty-one cores set it, and the frontend loaded the file
   anyway — invisible while those cores were handed small files, and 1.8 GB on a
   4 GB machine at The Warriors. Fixed, and re-checked by running a fullpath
   hardware core (Ikaruga on Flycast, 35,908,299-byte state) and a fullpath
   software one (Crash Bandicoot on pcsx_rearmed, 4,456,448) after the change.
3. **The frame's alpha channel was being obeyed.** See *Video: two paths*.
4. **`catalog::optionOverrides` reached the AUDIT and not the launch path.** It
   was called in one place, `--core-options`, and nowhere a game is actually
   started. While the table was empty that was invisible; PPSSPP's first real
   override is what exposed it. **An override table that is printed rather than
   applied is worse than no table**, because the audit agrees with itself.

##### Loose ends, all small, none blocking

- **`--state-test` cannot answer for this core.** Its warm-up is a tight loop of
  `retro_run` with no wall clock in it, and PPSSPP is the only core in the set
  that emulates on a thread of its own: three thousand calls produce no sound, a
  static picture and a zero-byte state, while the same core reaches its attract
  demo on the ordinary launch path. So a capture now reports
  `retro_serialize_size` instead, which answers half the question — the core can
  produce a 40 MB state — and leaves "is the round trip exact" open. **A core
  with its own emulation thread is not frame-deterministic under that test**,
  which is a real limitation of the instrument rather than a fault in the core.
- **The process aborts at exit if it is killed while a PSP game is still
  running** — `terminate called without an active exception`, after the capture
  and the summary have been written. It does **not** happen on the path a person
  takes: quitting through the overlay unloads the core first and exits 0,
  verified on both PSP games. Cause not established.
- **The audio governor is implemented for this core and has never engaged.**
  Cabinet measured Lumines at exactly 2.0x on an Apple TV, because PPSSPP's GL
  emu thread produces one SWAP per `retro_run` and a swap is a game frame rather
  than a vblank. Measured here, the ratio is one vblank per run — Lumines 709
  audio frames per run, Hammerin' Hero 682, against 735.8 for one vblank. The
  leading suspicion is this document's own rule: `ppsspp_frame_duplication`
  defaults to enabled and Cabinet leaves it unanswered, which is `false`. Not
  established.

#### The save audit, 2026-09-17: every core, against the bytes on the server

**MMagTech, after the PSP correction: "can you audit the other cores and see if
they suffer the same issue or similar."** Run against all **81 saves** on the
live RomM server — downloaded and fingerprinted by their actual first bytes,
not by their extension — and cross-read against Cabinet's `MemoryCardSync` and
this console's own save directory after real runs.

It answers two different questions, and the second one matters more.

##### Question one: is anything else wrapped in a foreign container? No. Only PSP.

| Platform | Tag | n | Size | First bytes | Verdict |
|---|---|---|---|---|---|
| PlayStation | `pcsx-rearmed-native` | 6 | 131072 | `MC` | native memory card |
| Dreamcast | `flycast-native` | 13 | 131072 | — | native VMU image |
| Saturn | `saturn-native` | 1 | 32768 | `BackUpRam Format` | native |
| TurboGrafx / CD | `pcefast-native` | 3 | 2048 | `HUBM` | native |
| Nintendo 64 | `mupen64plus-native` | 7 | 296960 | — | native combined save |
| Nintendo DS | `melonds-native` | 5 | 512 / 262144 | `MKDSSV10` | native |
| Game Boy Advance | `mgba-native` | 3 | 8192 / 32768 | `AGB  KIRBY` | native |
| Game Boy / Color | `gambatte-native` | 4 | 8192 / 32768, 8 | — | native; the 8-byte one is the RTC, its own region |
| 3DO | `opera-native` | 4 | 32768 | `.ZZZZZ..opera fo` | native |
| Sega CD | `gpgx-native` | 5 | 8192, 524288 | — | native; the 512K is the cart, its own region |
| Game Gear | `gpgx-native` | 2 | 3840 / 8193 | — | native |
| Neo Geo Pocket | `ngp-native` | 1 | 272 | `S` | native |
| Arcade | `fbneo-native`, `mame2003plus-native` | 17 | 64 … 131072 | varies | native NVRAM |
| GameCube | `dolphin` | 3 | 2 MB / 16 MB | — | native raw card |
| PlayStation 2 | `pcsx2` | 4 | 8650752 | `Sony PS2 Memory` | native |
| **PSP** | **`ppsspp-native`** | **1** | **51426** | **`rtfd`** | **Apple container** |

**PSP is the only one.** Every other core uploads the emulator's own bytes, so
anything that can read a save for those platforms can read what is on this
server. The `.srm` extension is a generic label rather than a claim about the
contents — misleading on about fifteen rows and harmless, because the bytes
underneath are native. Two platforms already get an honest extension (`.ps2`,
`.raw`) and two regions get their own (`.rtc`, `.cart`), which is the pattern
PSP should have followed.

**Two junk rows worth cleaning up on the server**, found by the same pass:

- **A 4-byte Arcade save containing the ASCII text `null`**, on Cotton Fantasy,
  under the tag `fbneo` rather than `fbneo-native` — so it is also the only row
  whose tag no current build writes.
- **A 131072-byte PlayStation card with `emulator: null`**, on Need for Speed
  III. It is a perfectly good memory card that no tag can match, so no client
  will ever offer it.

##### Question two: which of these can this console actually sync? Fewer than half.

This is the finding that matters, and the audit is what made the size of it
visible. CabinetOS's sync layer knows exactly one mechanism,
`RETRO_MEMORY_SAVE_RAM`. Sorting the 81 rows by the mechanism their platform
actually uses:

| | Saves on the server |
|---|---|
| Ride `RETRO_MEMORY_SAVE_RAM` — CabinetOS handles these | **34** |
| Written by the core as a FILE — CabinetOS handles none of them | **47** |

**Fifty-eight percent of the saves on this server are for platforms this console
can neither upload nor restore today.** The handover has carried that as "the
file-writing save class is not synced at all", which is true and reads like an
edge case. It is the majority.

And none of it is a design problem, because Cabinet has already solved each one
and the recipes are specific:

> **THIS TABLE WAS READ OUT OF THE REFERENCE IMPLEMENTATION AND THREE OF ITS
> ROWS ARE WRONG HERE.** Corrected 2026-09-19 by running each platform and
> reading the directory afterwards; the corrections are in the section below,
> *BUILT AND MEASURED*, and the live version of the table is
> `catalog::saveFiles`. Kept as written because the three that moved are the
> useful part.

| Platform | Where the core writes it | Name | On this console |
|---|---|---|---|
| **Dreamcast** | the **system** directory, `dc/` | `vmu_save_A1.bin`, or `<gameId>_vmu_save_A1.bin` with per-game VMUs | nothing, and it shows |
| Arcade — MAME | save directory | `nvram/<stem>.nv` — **wrong, it is one directory deeper** | **already on disk** from a real run |
| Arcade — FBNeo | save directory | `fbneo/<stem>.fs` | — |
| 3DO | save directory | `opera/shared/nvram.0.srm` — **only with two options forced** | — |
| Sega CD | save directory | `*.brm`, plus `*cart.brm` as its own region — **`*.brm` is named after the BIOS region unless an option is forced** | **already on disk** (`scd_U.brm`) |
| Neo Geo Pocket | save directory | `*.flash` | — |
| Nintendo DS | save directory | `*.sav` | — |
| PSP | save directory | the `PSP/SAVEDATA/**` tree | **already on disk** |

##### Dreamcast is the one to do first, and the audit explains a symptom this document already had

Thirteen saves — **the largest count of any platform on the server** — and this
console cannot see any of them. It also explains, exactly, why *"Ikaruga opens
on memory card not connected"* has been recorded here for two days as a
curiosity of the file-writing save class:

- Flycast never exposes the VMU through `RETRO_MEMORY_SAVE_RAM` at all. Cabinet
  confirmed that against the core's own `retro_get_memory_data`, which only ever
  answers `RETRO_MEMORY_SYSTEM_RAM`.
- It reads and writes a real file in the **system** directory, `dc/`, not the
  save directory — which is the same `dc/` the BIOS lives in, and is why this
  console has `system/dc/dc_nvmem.bin` and nothing beside it.
- Cabinet **restores the card there before the core boots** and captures it
  after unload. Nothing here does, so the machine boots with an empty slot and
  the game says so.
- Verified on this console: `reicast_device_port1_slot1` is answered `VMU`, so
  the port is configured — the card itself is simply absent.

So Dreamcast save sync is: write the bytes to `bios/dc/vmu_save_A1.bin` before
boot, read them back after unload, upload if changed. **With thirteen real cards
on the server to test the restore against**, which is a better test bed than any
other platform offers.

**DONE 2026-09-19, and the diagnosis above was right about the file and wrong
about the cause of the symptom.** The card was already reaching
`bios/dc/vmu_save_A1.bin` and Ikaruga still opened on "memory card not
connected", because a VMU lives in a CONTROLLER'S expansion socket and this
frontend was never calling `retro_set_controller_port_device`. See the section
below.

##### Two guards to copy rather than rediscover

- **A uniform fill means the game never saved.** MAME's fresh NVRAM is all `0x01`
  for the capbowl family and all `0x00` elsewhere, and a seeded bootstrap image
  is identical for everyone who plays that board. Cabinet refuses to upload one
  (`isUntouchedNVRAM`), because otherwise every launch fills somebody's RomM with
  rows carrying no history — which are then pulled down onto their other device
  as if they meant something. The audit shows the guard works: several arcade
  rows exist and none is a uniform fill.
- **Sega CD's cart is not its internal RAM.** The scan must exclude `cart.brm`
  from the `.brm` match and give it its own region, or one overwrites the other.
  Games prefer the cart when present.

##### One more gap, smaller, found on our own disk

`romcache/saves/pcsx-card2.mcd`, 131072 bytes — **PlayStation memory card 2**.
Card 1 rides `RETRO_MEMORY_SAVE_RAM` and syncs; card 2 is a file and syncs on
neither Cabinet nor here. Nothing on the server has ever held one. Low stakes,
but it is the same shape as everything above and should be written down rather
than found again.

#### BUILT AND MEASURED, 2026-09-19: the file-writing class syncs, all seven platforms

**In one sentence: a save made on an Apple TV now arrives on this console and a
save made here reaches the server, for the 47 of 81 rows that could do neither.**
Seven platforms, each proved against real saves already on the reference server
rather than against files this session made up.

`frontend/src/filesave.{h,cpp}` is the mechanism, `catalog::saveFiles` is the
table, and the two halves hang off the same triggers PSP's directory save
taught: restore BEFORE `retro_load_game`, capture AFTER `retro_unload_game`,
compare against a baseline taken at launch and send only what moved.

##### What each platform actually does, measured on the test VM

Every row is a real save that was already on the server, pulled down, read by
the core, written back by the core, and sent up again — with the hash checked
at both ends.

| Platform | The core's file | Round trip |
|---|---|---|
| Dreamcast | `bios/dc/vmu_save_A1.bin` | Ikaruga, 131072 B, byte-identical |
| Sega CD | `<stem>.brm` **and** `4Mbit_cart.brm` | Lunar, 8192 B and 524288 B, both |
| Arcade — MAME | `mame2003-plus/nvram/<stem>.nv` | Lethal Enforcers, 128 B |
| Arcade — FBNeo | `fbneo/<stem>.fs` | Smash T.V., 32768 B |
| 3DO | `opera/shared/nvram.0.srm` | Gex, 32768 B |
| Neo Geo Pocket | `<stem>.flash` | Metal Slug 1st Mission, 272 B |
| Nintendo DS | `<stem>.sav` | Contra 4, 512 B |

**Seven uploads and the server still holds 81 rows.** That number is the whole
of the filename decision below: every one of them replaced the reference
implementation's own row rather than sitting beside it.

##### Three of the table's rows were wrong, and only running them said so

The audit's per-platform table was read out of the reference implementation.
Three lines do not survive contact with this console, and each was found by
launching a game and reading the directory afterwards.

- **MAME writes one directory deeper.** `mame2003-plus/nvram/<stem>.nv`, not
  `nvram/<stem>.nv`. Corroborated twice: by the run, and by the orphaned NVRAM
  the old flat save pile left behind, which sits at exactly that path. Placing
  it at the shallower path is not a harmless miss — the core does not find it,
  bootstraps a fresh image, and writes THAT, so the restore silently does
  nothing and the capture finds nothing either. The first run did exactly this
  and produced a plausible-looking 128-byte file that was not the save.
- **Sega CD's internal backup RAM is named after the BIOS REGION**, `scd_U.brm`,
  not after the game — because on real hardware it is the console's own 8 KB
  shared by every disc. The reference gets `<stem>.brm` only because it forces
  `genesis_plus_gx_system_bram` to `per game`, and this console was answering
  that option with the core's declared default. The first run restored Lunar's
  real card to `<stem>.brm` and the core ignored it and made a fresh `scd_U.brm`
  beside it.
- **3DO's fixed path depends on two forced options**, and without them the core
  does not start at all: `opera_bios` has a declared default of `disabled`
  while its value must be a BIOS FILENAME, and `opera_nvram_storage` has a
  declared default of `per game` against a code fallback of `shared`. Both are
  now in `catalog::optionOverrides` with the reasoning beside them, which is
  the first slice of the handover's *Finish the core options*.

**The lesson is the one this project keeps relearning, in its sharpest form
yet: a fact carried across is a fact nobody has checked.** Three of eight rows
in a table taken from a working implementation were wrong here, all three
failed silently, and all three would have shipped.

##### The guard the reference does not have, and the 23% it explains

**Three of the thirteen Dreamcast cards on the server hold no save at all** —
Cannon Spike, Re-Volt, San Francisco Rush 2049. They are formatted, empty cards
uploaded because the reference implementation applies no freshness rule to
Dreamcast: its Dreamcast capture compares against the previous local copy and
nothing else, so a session that started with no card at all uploads whatever
the core formatted. Nearly a quarter of the rows for the platform with the most
of them carry nothing.

A VMU says outright whether it holds anything, so this console asks it. The
card is 256 blocks of 512 bytes; block 255 is the root and begins with sixteen
`0x55` bytes; blocks 253 down to 241 are the directory, sixteen 32-byte entries
each, and an entry's first byte is `0x33` for a data file, `0xCC` for a game and
`0x00` for a free slot. All thirteen real cards carry the root signature and ten
of them have at least one entry. It is the Dreamcast analogue of the PS1
block-header test the reference already uses.

**Demonstrated rather than asserted.** Power Stone has no card anywhere —
nothing local, nothing on the server. Launched it, Flycast formatted a fresh
VMU, and the quit said:

```
[save] no save anywhere for dc/vmu_save_A1.bin yet
[save] ./bios/dc/vmu_save_A1.bin is what the core writes by starting up,
       not a save — not sending it
```

No row on the server, no file under the person, and the card still taken out of
`bios/`. The other rules are the reference's own, kept: a uniform fill for
arcade NVRAM, the first 16 and last 64 bytes skipped for Sega CD, the first 176
skipped for 3DO. All of them apply only when no real save existed before —
once one has, every later change travels, an erase included, because losing
history is worse than an empty row.

##### The filename decides whether a person has one memory card or two

`POST /api/saves` is sent with `overwrite=true` and **RomM matches a row for
overwrite by FILENAME ALONE** — the emulator tag is not part of it. So the name
is what decides whether a card is one row across all of somebody's devices or
one row per device, and this console was quietly choosing the second: it named
rows after the game's TITLE while the reference names them after the server's
own `fs_name`. It is visible on the server today, and was before this work —
Lumines has a `Lumines - Puzzle Fusion (USA) (Cabinet).srm` from an Apple TV
and a `Lumines.zip` from here, both tagged `ppsspp-native`, both the same save.

Every save this console uploads is now `<fs name without extension>
(Cabinet).<region>`, which is the reference's own convention:

- the `(Cabinet)` marker keeps the row distinct from anything RomM's own web
  player wrote, which a bare `<name>.srm` would silently take over;
- arcade puts the core inside it — `(Cabinet fbneo)`, `(Cabinet mame2003Plus)` —
  because one game legitimately has two of these and they must not overwrite
  each other;
- the region is the extension, so Sega CD's cartridge is `.cart` and lands in
  its own row rather than on top of the internal RAM.

**What it costs:** a row this console wrote under the old name stops being
updated and a correctly named one appears beside it. Nothing is lost — a
restore takes the newest row for the tag whatever it is called — and the orphan
can be deleted by hand.

**PSP is the one exception and it is deliberate.** Its row stays `<title>.zip`,
because the reference's only PSP row is an Apple `rtfd` archive wearing an
`.srm` extension and the fixed build that writes a zip has not been seen from
here yet. Renaming ours onto that row would overwrite a save with a container
the other end may not read. Settle it from the first save that build uploads.

##### A save tag is not a state tag, and five platforms needed the difference

`catalog::emulatorTag` is deliberately silent for Flycast, Opera, FBNeo, MAME
2003-Plus and Beetle NGP, and silence means "do not upload". Those five hold
**35 of the 47** file saves on the server, all thirteen Dreamcast cards
included, so the strict rule would have left the majority of this feature dead
on arrival.

It would also have been the wrong rule. **A state is a photograph of the
emulator's insides and the tag is the promise that the build about to load it
is the build that wrote it. A file save in this class is the emulated machine's
own storage** — a VMU image, a board's NVRAM chip, a Sega CD's backup RAM — and
its format is defined by the hardware, not by the emulator. The audit proved
exactly that by reading the bytes: every core but PPSSPP uploads the emulator's
own bytes, so anything that can read a save for those platforms can read what
is on the server.

So `catalog::saveTag` exists beside `emulatorTag`, returns the state tag
wherever there is one, and adds those five with the reference's own strings.
**Flycast is the case that shows the two apart:** its pinned commit does not
reproduce what the reference ships, because that build carries unscripted edits
in its working tree, so `emulatorTag` still returns nothing for it and no state
is ever uploaded — and `saveTag` returns `flycast-native`, because nothing about
an unscripted edit changes the shape of a VMU.

##### The VMU is out of `bios/`, which closes the folder layout's last gap

Open question 18 left one thing unanswered: `bios/` is meant to hold
replaceable firmware, libretro gives a core exactly ONE system directory, and
Flycast keeps the Dreamcast's card in it — the single file in there that could
never be fetched again.

The card is now placed in `bios/dc/` for the length of a session, because
Flycast will look nowhere else, and taken back out into
`users/<id> - <name>/saves/Sega Dreamcast/<romId>/flycast/` at the quit. **After
a clean quit `bios/dc/` holds `dc_nvmem.bin` and nothing else** — the console's
own clock and language, a machine fact that rebuilds itself if lost. Checked by
listing the directory, not by reading the code.

**A session that does not quit cleanly leaves the card there**, and the next
launch would otherwise write over it. It does not: a card found in the system
directory that does not match this game's own copy is moved to
`users/<id> - <name>/saves/unattributed/system-directory/` with a timestamp.
Nothing on the machine says which game wrote it, so it is kept rather than
guessed at — the same answer the folder move gave to the two piles it could not
attribute, in the same place.

##### Three bugs this work found by running things, which nothing else would have

None of the three is about saves. All three were invisible to the build
pipeline and to every screenshot taken before.

1. **No arcade game could start, and had not been able to since the folder
   layout landed.** `cores/build-core.sh` names FBNeo's artifact
   `fbneo_libretro.so` and `catalog::coverageFor` looked for the same, but the
   LAUNCH path appended `_libretro.so` to whatever the manifest called the
   core — so it opened `fbneo_libretro_libretro.so`, a file nothing builds. The
   rule now lives once, in `catalog::coreFileName`.
2. **MAME and FBNeo could not find their machines.** Both pick the driver from
   the loaded file's NAME — `lethalen.zip` is the Lethal Enforcers driver — and
   the folder layout collapses a single-file entry to `<romId> - <title>.zip`.
   The core looked up `3022 - Lethal Enforcers`, found nothing, and the launch
   ended at *"Game driver not found"*. An entry whose core opens its own
   archive now stays a DIRECTORY holding the server's own file name:
   `cache/MAME2003/3022 - Lethal Enforcers/lethalen.zip`. The layout already
   allows either shape and renames both identically, so keeping and releasing
   do not care.
3. **The frontend never told a core what was plugged in**, and that is what
   *"Ikaruga opens on memory card not connected"* has been for two days. A VMU
   lives in a CONTROLLER'S expansion socket, and Flycast builds the Dreamcast's
   Maple bus out of `retro_set_controller_port_device` calls this frontend was
   not making. **Telling it about some ports is the same as telling it about
   none** — Flycast returns early while any of its four ports is still unset, so
   the code that reads `device_port1_slot1` never runs. The host now learns the
   port count from `RETRO_ENVIRONMENT_SET_CONTROLLER_INFO`, which it used to
   acknowledge and throw away, and answers every port: a joypad on port 0,
   nothing on the rest, because this frontend drives one pad.

   **Found by photographing the screen.** The log said nothing, the file was in
   the right place, and the restore reported success. What settled it was the
   picture — 「メモリーカードが未接続です」 before, and
   「データファイルのロードに成功しました」 after.

##### What is still not done

- **No save in this class has been written by actually PLAYING a game here.**
  Every round trip above restored a real save, watched the core read it, and
  sent back what the core wrote — byte-identical, which is the correct answer
  for a session that saved nothing. Forcing the upload needed `--sync-test`,
  which now drops the file-save baselines at frame 150 so the quit-time capture
  sends whatever the core flushed. A headless VM cannot press the buttons that
  make a game save; the first real in-game save is a controller away.
- **An arcade entry that was already collapsed stays broken.** The cache is
  disposable and re-downloads, and the two on the test VM were deleted by hand,
  but nothing detects the old shape and re-makes it.
- **PlayStation memory card 2** is still a file nothing syncs, on this console
  or the reference. Nothing on the server has ever held one.
- **`opera_bios` is a filename written down in this repository and a filename
  on somebody's server**, and nothing checks that they agree. The reference
  stages whatever 1 MB firmware the platform has UNDER that name, which is the
  stronger answer, and is worth building the day a server calls it something
  else.

#### The test that answers the whole question, and can be run this week

The parity risk is not theoretical and it does not need CabinetOS to exist to
measure. **Cabinet already ships two different configurations of the same core.**

For the experiment to mean anything the two builds must differ *only* in the CPU
backend — same commit, same patches. The recovered manifest says which cores
qualify, and it narrows the field:

| Core | Same commit everywhere? | tvOS | macOS | Usable? |
|---|---|---|---|---|
| **melonDS** | yes — `66b5d263` | interpreter | `JIT_ARCH=aarch64` | **clean** |
| **Flycast** | yes — `a172e000` | `-DTARGET_NO_REC` | recompilers on | **caveat** |
| pcsx_rearmed | **no** — and tvOS revision lost | interpreter | `DYNAREC=ari64` | confounded, drop it |

> **Write a save state on the Mac and load it on the Apple TV, for melonDS and
> Flycast.** If it loads, CPU backend does not affect state format, and
> CabinetOS can take the faster Linux defaults — proper recompilers for
> Dreamcast, DS and PS1 rather than interpreters, which on Vega integrated
> graphics is the difference between comfortable and marginal. If it does not,
> every core must be built with Cabinet's exact backend, and that becomes a hard
> line in the manifest.

**melonDS is the clean experiment.** One tree, one revision, all four patches
applied to every platform, and the only difference is the recompiler.

**Flycast carries a caveat** and it is the unscripted-edits problem above: both
platforms build from one shared checkout, so if a hand edit was made *between*
the tvOS build and the Mac build, the two came from different tree states and
the result means nothing. Capture that diff first. If the answer from melonDS
and Flycast disagree, believe melonDS.

That is a one-evening test on hardware the project already owns, it needs no
Linux toolchain, and it is the highest-value thing anyone can do for Phase 5
right now. **Until it is run, assume states are backend-sensitive and match
Cabinet's flags exactly.**

#### Still open after Phase 0

- **Flycast's unscripted working-tree edits.** The most urgent item on the
  project, and it is Cabinet-side. Until that diff is captured, Flycast cannot
  be reproduced on any platform, CabinetOS included.
- **The manifest has no `linux` row, and several `build_args` are null.** The
  nulls are honest — `build-core.sh` passes no extra make arguments for those
  cores, so the core's own Makefile platform case decides. **But the Linux case
  decides differently**, and picodrive and Mupen64Plus are exactly the two where
  a null reads as "nothing to match" while the Linux default quietly turns on a
  recompiler (`use_sh2drc`, `WITH_DYNAREC=x86_64`). When CabinetOS adds its
  rows, every one of them must be explicit — never null — even where the value
  is "the default".
- **The save-state backend question above.** Untested. Blocks nothing until
  Phase 5, but shapes the manifest.
- **Realigning the eleven diverged cores.** Cabinet-side, its own release, with
  the core quality pass re-run. Best done before there are users with save
  histories.
- **The `emulator` tag carries no version.** See *How Cabinet hosts cores*. A
  state written by a mismatched build is offered as loadable, because the tag
  cannot tell. **There is now a cheap fix**: several cores compile their own git
  revision into the version string they report, so the frontend can assert the
  loaded core against the manifest and Cabinet can put the revision in the tag.
  Verified on Gambatte — see Phase 3. Needs checking per core, and the Cabinet
  half is a Cabinet-side change.
- ~~**Nothing has been compiled.**~~ **DONE, 2026-09-13.** Gambatte built for
  Linux x86-64 at its pinned commit with `make platform=unix`, **first attempt,
  zero patches**, and Dr. Mario runs on it. See Phase 3. The remaining twenty
  are now a loop rather than a question — but they are still twenty, and the
  backend-sensitive ones still need their flags set explicitly.
- ~~**Firmware.**~~ **HANDLED 2026-09-17, and it was the special case this said
  it would be.** PPSSPP's system files ship with the emulator rather than coming
  from RomM, so `build-core.sh` installs Cabinet's 43-file subset of upstream's
  `assets/` into the frontend's system directory as `PPSSPP/`. The core checks
  for `compat.ini` there and warns if it is missing, which is how the audit
  caught that it was not setting directories at all. **Where they live in the
  IMAGE is still Phase 5's** — a path in `/usr`, and the core only ever reads
  them.

### Prior art: how Cabinet and Grout already do this

Read 2026-09-13. **Cabinet has already solved most of the storage problem, and
CabinetOS should inherit its model rather than invent one.**

#### Cabinet — `docs/scope-native-offline.md`, `scope-download-all.md`

- **"Keep on device" already exists**, and the cached/kept distinction in this
  document matches Cabinet's exactly: a per-game toggle, permanent storage shown
  with its size, removable where it was added, and explicitly *not* a cache —
  "caches serve speed, kept games serve a promise". Keeping a game pulls its ROM
  **and its platform's firmware**. Keyed by rom id.
- **The kept-game manifest embeds the whole `Rom` object**, not a hand-picked
  subset, which is what makes offline navigation work: cover art, platform
  label and metadata are all present with no server. This matters enormously for
  portable drives — see below.
- **Saves are written locally first**, into a per-game `pending-states`
  directory, before any attempt to reach RomM. Conflicts are *designed out*
  rather than resolved: each queued file carries RomM's own timestamped name, so
  an upload lands exactly as if it had happened online and nothing overwrites
  anything. Sync is only "finish the uploads", and is safe to run often.
- **Save state caching is opportunistic, not queued.** A state is cached when a
  game is kept and refreshed on ordinary online visits. The reasoning, reached
  before building: Cabinet already fetches live whenever online, so the only gap
  is between the last check and losing signal — closed by refreshing on ordinary
  use rather than by a background job.
- **Offline is one signal, not two.** `NetworkMonitor` combines real
  disconnection with a deliberate Offline Mode toggle into a single `isOffline`
  that every screen asks, so both drive identical code paths.
- **States are never exposed to other apps**; ROMs are. A state blob is
  core-format-specific and useless elsewhere, a ROM is not.
- **Download All exists on Mac and iOS but deliberately not tvOS**, on the
  grounds that "a television keeps a handful of games and has the disk for
  that". **CabinetOS should reverse that call.** It is television-shaped but has
  a large dedicated drive and a user who explicitly chose where games live —
  which is the case Download All is for.

#### Grout — RomM's own Linux handheld client

- Pulls ROMs to the device in **the host frontend's expected folder layout**
  (muOS/NextUI), not a layout of its own. Pushes on session end, on idle, or on
  a schedule. Fully playable offline between syncs.
- Matches saves to games by **platform plus filename**, case-insensitively, with
  PSP's directory saves matched by Game ID instead. Not by hash, and not by
  RomM id.
- Conflicts are surfaced, not resolved automatically: a per-game screen
  defaulting to Skip, where the user picks Keep Local or Keep Remote.

#### The finding that matters most

**Grout syncs save files only. It explicitly refuses to sync save states**,
because states "require both sides to use the same emulator and sometimes even
the same version".

That is RomM's own first-party client independently confirming the core-parity
constraint in *Emulation* above — and it draws the opposite conclusion, because
it cannot control what emulator the handheld runs.

Cabinet **can** sync states, and does, precisely because it controls both ends
and ships identical cores. That is not a minor feature difference; it is the
thing Cabinet does that the rest of the ecosystem cannot.

**So core parity is not a nice-to-have that makes saves more convenient. It is
the entire reason CabinetOS can offer continuity at all.** Get it wrong and the
product degrades to what Grout already does for free.

### 14. User-selectable game storage
**Raised: Phase 1. Mostly DECIDED. Design in Phase 4, UI in Phase 6/8.**

The user picks where games are stored — internal drive, second SSD, or USB.

**Binds Phase 4 immediately:** the storage path is configuration from the first
line of code, never a constant, and the cached/kept distinction applies per
location rather than globally.

#### Decided

**REVISED 2026-09-16: a second drive takes the KEPT games, and the cache stays
on the internal disk.** MMagTech, thinking ahead to testing it. The paragraph below
said one active location holding everything, and the split is better, because
the two things are different in kind:

| | |
|---|---|
| **Kept games** | deliberate, permanent, and the whole point of a drive that travels |
| **The cache** | "what this machine happened to play" — meaningless to carry, and better on the disk that is always attached and usually faster than USB |

**And it quietly removes the update hazard.** The system reserve exists because
kept games are the one thing the console will not delete; put them on a
different drive and they cannot fill the system disk at all. The reserve stays
for the single-drive case, which remains the common one.

**What travels on the drive, and what does not.** Everything here is a copy of
something on RomM, so the drive only earns its keep for things that are slow to
replace or impossible to:

- **Games: yes.** Hundreds of gigabytes, and re-fetching them on the other
  console is the cost this avoids.
- **Synced saves, memory cards and states: no.** Kilobytes, already on the
  server, and carrying a second copy creates two versions of the truth — which
  this question has already settled the other way, in *"a game continued on a
  different server starts from that server's save history"*.
- **Anything not yet uploaded: NO, and this changed within the hour.** It was
  "yes, because the drive travels and progress would be stranded" — and then the
  drive stopped travelling, see the reversal below. Save data of every kind stays
  on the internal disk, so unplugging the drive cannot strand anything.

> **The drive carries what is slow to replace and what cannot be replaced. Not
> what the server already has.**

**Still true, and the reason the paragraph below is revised rather than
deleted:** migration between locations is still a first-class operation, a
missing drive still degrades rather than errors, and the drive is still
self-describing so it can be browsed against a different server.

**One active location, with migration between them.** Upgrading to a larger
drive is a normal thing to want, so moving the library is a first-class
operation rather than something the user does by hand. It must survive being
interrupted — power cut, unplugged cable — and resume, without losing a kept
game or leaving two half-copies.

**A missing drive degrades; it never errors.** If the game drive is absent,
games simply come from RomM again. That falls straight out of RomM being the
source of truth: the local copy is a cache, and a cache that has gone away is
re-fetched, not mourned. The console starts normally and stays fully usable.

The UI must still *say* so — plainly, once, somewhere visible — because
silently re-downloading a library over Wi-Fi is its own kind of rude. Kept games
whose drive is missing should be listed as such, since the kept flag lives in
configuration rather than on the drive itself.

**Unresolved within this:** save states written since the last sync live only on
that drive. Losing the drive is harmless for ROMs and not harmless for those.
Phase 4 should sync saves aggressively enough that the window is small, and the
warning should be honest about it.

**Formatting: not decided, not ruled out.** There is console precedent — the
PS5 formats an internal M.2, the Xbox formats external drives — so it is not
inherently un-console-like. If it happens: never the default action, a
confirmation that cannot be fumbled through on a controller, and prefer adopting
a drive as-is wherever possible.

#### REVISED AGAIN, 2026-09-16: a drive belongs to a SERVER, not a console

**MMagTech, within the hour, and it is better than binding to a console for a
reason that is obvious once said: the games on the drive are already
server-specific.** They are identified by RomM's own rom IDs, which mean nothing
on any other instance. Binding to the server states what is already true;
binding to a console invented a second, weaker notion of ownership on top of it.

It fixes both failure cases the console version had:

| | |
|---|---|
| **Two consoles, one house, one server** | the drive works on either — which was the original motivation this whole question was raised for |
| **The console dies and is replaced** | new machine, same server, plug it in, it works. No "this belongs to another console" prompt to design |

And the case that drove all the complexity — a drive meeting a **different**
server — stops needing a solution. It is refused. Every piece of machinery in
the superseded section below (self-describing manifests, hash matching,
adoption into a foreign library) existed only to serve that case.

##### RomM will not tell us which server it is, and it turns out not to matter

**Checked against the live server rather than assumed.** `/api/heartbeat`
returns a version and the enabled metadata sources; `/api/stats` returns counts.
**Neither carries an instance identity**, and there is nowhere on the server to
write one — the console's token covers assets, not arbitrary storage.

The address will not stand in for it either: an IP changes, a hostname replaces
it, someone puts https in front, and the same server reads as a different one.

**Two answers were designed here and both were too much.** The first sampled
the drive's games to decide whether it was "ours", which MMagTech broke in one
sentence — people delete games from the server, so the sample misses and four
terabytes get condemned as somebody else's. The second checked every game
against the server on plugging in, with a Storage screen for the leftovers.

**MMagTech's third answer is that none of it needs building, and he is right,
because the check already exists.** `beginLaunch` will not reuse a downloaded
file unless it sits at that game's rom-id path, under the name the server gave,
at the size the server reported:

```c
if (struct stat st; expectedSize > 0 && ::stat(dest.c_str(), &st) == 0)
    haveIt = st.st_size == expectedSize;
```

**That test does not care which disk the file is on.** Point it at an external
drive and everything the elaborate versions were for comes free:

| | |
|---|---|
| A game deleted from the server | never asked for, because it is not in the library |
| A file that does not match the record | not reused. It re-downloads rather than launching the wrong game |
| A drive from another server | nothing matches, so nothing is used, and nothing is destroyed |
| A server that moved address | everything still matches; the address is not part of the test |

> **So there is no drive identity, no verdict, no adoption, no erase prompt and
> no new code.** The expectation — one drive, one server — is a sentence of
> documentation rather than a mechanism.

**There is nowhere to put that sentence yet**, which is worth saying rather than
pretending otherwise: the README covers building and installing, and the product
has no user-facing documentation at all. It goes wherever that ends up, and this
is the second item waiting on it — *Emulation* already owes the same for what
keeping a game means.

**And nothing on a drive is ever deleted because it was not recognised.**
Unrecognised files are simply not used. That keeps the console away from the one
class of data RomM cannot give back, without needing a rule to say so.

#### DECIDED 2026-09-19: plug it in and it works, and never two copies of a game

**MMagTech's ask, after the folder layout landed: "I want the easiest most
seamless experience for a second drive whether it's a second internal drive or a
USB you plug in."** What the console does about a drive — not what the drive is
for, which is settled above.

Checked against what the consoles people already own actually do, rather than
recalled:

| | |
|---|---|
| **Switch** | Put a card in and it becomes the download location. **No setup screen.** If it fills, it falls back to internal on its own. |
| **Switch** | Save data is **never** on the card — *"stored on the console's System Memory... in order to keep it safe."* The same rule this question reached independently, with the same reason. |
| **Steam** | One library per drive, one of them marked Default, and *Move install folder* per game. The same shape as `roms/`+`cache/` repeating per location. |
| **Steam Deck** | **Gets removal wrong, and that is the finding worth having.** Pull the card and the games still show as installed with a green Play button that does nothing; it does not notice a physical removal at all. Exactly what *"a missing drive degrades; it never errors"* forbids. |
| **PS5** | A USB drive may HOLD a PS5 game but not run it, so you move it back to play. A tier this console does not need — everything here is a copy of the server. |

**So: six rules, and every one of them is the console not asking a question.**

1. **Never take over the drive.** One folder named `CabinetOS/` on it, and only
   that. No formatting, no wizard, no adoption prompt. A drive with somebody's
   films on it also works as a games drive and nothing of theirs is at risk.
2. **Plug it in and it is used.** It becomes where kept games go. The Switch's
   answer, and it removes a screen that would otherwise wait on the reference
   machine.
3. **A second internal drive and a USB stick are the same thing** — another
   place with room. The PS5 distinguishes them for a speed reason this console
   does not have.
4. **Saves never go on it.** Already decided above; Nintendo says the reason out
   loud and it is the right one.
5. **Look at the disk, do not remember what was on it.** This is the one line
   that makes the Steam Deck bug impossible here: `cache::find` does a readdir
   at the moment somebody presses Play, so there is no cached list to go stale
   and no hot-plug event to miss. An unplugged drive simply means the game is
   not found, and not found already means fetch it.
6. **Say it once, then behave normally.** *"Your games drive is not connected"*
   the first time and nothing after, because silently re-downloading a library
   over Wi-Fi is its own kind of rude.

##### The duplicate, which MMagTech found and is the only real hole in it

**Keep a game with the drive plugged in, unplug it, play the game — it comes
down from RomM into the cache. Plug the drive back in and the game is on the
machine twice.**

Leaving both is not acceptable: two copies of a 40 GB title sitting there until
something happens to need the room is exactly the sort of thing a console should
never do. **So the redundant copy is deleted, on the spot.**

That is safe, and provably rather than probably: the two files are the same
game at the same size their server reports, one of them has just been read, and
even losing both costs a re-download. It is the same size check the download
path already trusts to decide a game is here and need not be fetched again.

**Which one wins is decided by what the game IS, not by which disk it is on:**

| | |
|---|---|
| Still kept | the **drive** copy wins — that is where kept games live, and it leaves the internal disk for the cache and the system reserve |
| No longer kept | the **internal** copy wins — that is where the cache lives, so the drive only ever carries what somebody deliberately asked to keep |
| One is the wrong size | the good one wins, whichever disk it is on, and moves to where its state says it belongs |
| **Neither** is the right size | **nothing is deleted.** Two suspect files and a guess is the one move here that could actually cost something. Fetch a clean one. |

**The console always knows the answer even with the drive in a drawer**, because
the keep record lives in `users/<id> - <name>/keeps/` on the internal disk and
never travels. And the check runs when somebody next plays that game rather than
when the drive appears, so it needs no detection and cannot go stale — rule 5
again.

#### BUILT 2026-09-19

`storage::locations()` looks for drives every time it is asked — under
`/run/media/<user>/` where udisks mounts a USB stick and `/var/mnt/` where an
fstab-mounted second internal disk goes — and takes any mount that is **on a
different filesystem from the internal root**, which is the check that stops a
folder on the internal disk being mistaken for a drive and then "lost". It
claims `CabinetOS/` on each and nothing else. `$CABINETOS_DRIVES` overrides the
search for testing.

`keepLocation()` is the first drive or the internal disk. `cache::dedupe`
resolves a game that is on the machine twice. The missing-drive line is said
once and the drive then forgotten, so it is never said twice.

##### The bug this turned up, which is the reason it was worth running

**A game fetched by PLAYING it was landing in `roms/`, where nothing may evict
it.** The keep record lives on the internal disk and survives the drive being
unplugged — which is the point of it — so asking *"is this game kept"* answered
yes, and the fetched copy was filed as a kept game on the internal disk.

Play it twenty times with the drive in a drawer and the console has filled its
own disk with games it is not allowed to delete. **That is precisely what the
system reserve exists to prevent, arriving through a door nothing was watching**
— the floors guard the Download button, and this was not the Download button.

The rule is now the product's own, stated in Phase 4 and forgotten here:
**Download is the one deliberate storage act.** So a fetch writes into `roms/`
only when the person is keeping the game with that press, and everything else
goes to the cache on the internal disk — where it is a stand-in, evictable,
costing a re-download at worst, and deleted outright the moment the drive
returns and `dedupe` sees the real copy.

##### The scenario, run end to end on the test VM

MMagTech's own, on a machine with two real filesystems — internal on device 37,
the drive on 58:

| | |
|---|---|
| Keep it with the drive plugged in | lands at `<drive>/CabinetOS/roms/Game Boy/2813 - Pokémon Red Version.gb`. **Nothing on the internal disk.** |
| Unplug it and press Play | *"the games drive at … is not connected — games kept on it will be fetched from RomM again"*, then fetched to `cache/Game Boy/2813 - …` on the internal disk. **Evictable**, which is the fix above. |
| Start again, still unplugged | **says nothing.** Reported once and the drive then forgotten. |
| Plug it back in and press Play | *"rom 2813 was here twice; kept `<drive>/…/roms/…` and removed `<internal>/cache/…`"*, then played from the drive. |
| Afterwards | one copy, on the drive. |

##### What still is not built

The **screen** that says the drive is missing. The console says it on stderr
once; the person-facing version is a picture and waits with the rest of the UI.
The games on that drive already behave correctly without it.

And a consequence worth knowing rather than fixing: **keeping a game that is
already in the internal cache leaves it on the internal disk.** Keeping never
moves bytes between drives — it renames the entry from `cache/` to `roms/` on
the location it is already on, which is instant whatever the game weighs.
`keepLocation()` decides where a download LANDS and nothing else. The
alternative is a cross-drive copy of gigabytes at the moment somebody presses a
button, which is the thing this whole layout is arranged to avoid.

#### SUPERSEDED — a drive belongs to one console

**The section below decided a drive should move between CabinetOS machines, and
built a self-describing drive to make it work. MMagTech reversed it the same day
the split above was agreed, and the reasoning is short.**

**The only real benefit of a portable drive is not re-downloading the games.**
Everything else — names, artwork, platform labels, saves, play history — comes
from RomM regardless. That single benefit does not pay for what portability
drags in: a drive that must describe itself, games to be matched against a
different server's library, and two consoles each holding half of somebody's
unfinished progress for the same game.

> **A drive is extra storage for the console it was attached to.** One owner.

**Superseded above**, by binding to the server instead. The paragraph that
followed here had to invent an adoption prompt so that a drive would not die
with its console — a problem that does not exist once the drive belongs to the
library rather than to the machine.

**And it simplifies the split agreed above.** If the drive never travels, save
data has no reason to be on it: **all saves, memory cards and states stay on the
internal disk**, and the external drive is purely game storage. Unplugging it can
then never strand anyone's progress, which was the fiddliest part of the previous
answer and is now simply gone.

**What survives from below:** migration between locations is still first-class,
a missing drive still degrades rather than errors, and a documented on-disk
layout is still worth having — not so another server can read it, but so that a
future version of CabinetOS can.

#### Portable drives, and the problem with them — SUPERSEDED, kept for the reasoning

**Decided: a drive should move between CabinetOS machines.** The obvious case is
two boxes in one house sharing a RomM server, and there the drive should simply
work.

**The tension, raised by MMagTech:** CabinetOS is tied to a RomM login. Move the
drive to a machine paired with a *different* server and the ROM files are
present but the library describing them is not — names, artwork, metadata,
collections and save history all live server-side, and game identifiers are
specific to a server instance.

**Better answer, from Cabinet rather than invented:** I proposed matching by
file hash. Cabinet already does something more useful — **its kept-game manifest
embeds the whole `Rom` object**, which is what lets it navigate a library
offline with real cover art and platform labels and no server at all.

Carry that onto the drive and the drive becomes **self-describing**. Plugged
into a machine paired with a different server, the games are still browsable and
playable, because everything needed to present them travelled with them. No hash
lookup, no dependency on the new server having the same content.

Neither Cabinet nor Grout uses hashes, incidentally: Cabinet keys on RomM's rom
id, Grout matches on platform plus filename. A hash index may still be worth
adding for *adoption* — recognising that a file on the drive is the same game
the new server already knows about, so it links up rather than sitting as a
duplicate — but it is an optimisation on top of a self-describing drive, not the
mechanism itself.

That gives three sensible tiers instead of a binary:

| Situation | Result |
|---|---|
| Same RomM server | Everything works. The common case. |
| Different server, same games | Browsable and playable from the drive's own manifest; adoption links them to the new server's library. |
| Different server, unknown games | Still browsable and playable from the manifest. Saves have nowhere to go. |

It also argues for a **documented on-disk layout** rather than an
implementation-defined one, since the drive is now a thing other software has to
understand.

What it does **not** solve is saves: those are server-side, so a game continued
on a different server starts from that server's save history. That is correct
behaviour rather than a bug, but the UI should not pretend otherwise.

**Follow Cabinet's save model rather than designing one.** Saves write locally
first, into a pending queue, and upload afterwards — losing signal mid-save
never loses the save. Conflicts are designed out by giving each queued file
RomM's own timestamped name, so uploads never overwrite and syncing is only
"finish the uploads". Grout instead surfaces conflicts for the user to resolve,
which is the right call for a client that cannot write the filename — and the
wrong one here, since we can.

### 15. A phone companion page for first run
**Raised 2026-09-13 as a design proposal. NOT ADOPTED YET — deferred, with
reasons. Revisit after the on-screen keyboard exists.**

> **The address has a machine-local home as of 2026-09-19, and it is not a
> screen.** Putting the frontend in the image made this urgent rather than
> theoretical: an installed console had no way at all to learn which RomM
> server it belongs to, because `--romm` is a command-line flag and a console
> has no command line. `/etc/cabinetos/session.env` holds it, the session
> script exports it as `CABINETOS_ROMM`, and README.md has the two commands
> that set it up on a fresh machine. **That is plumbing, not an answer to this
> question** — somebody still has to reach the console over SSH — but it means
> whatever screen is eventually built has one file to write rather than a
> mechanism to invent, and it means the machine boots usefully in the
> meantime. A console with no such file comes up on the stand-in library.

The proposal: CabinetOS serves a small web page during first run and shows a QR
code pointing at itself, so the RomM address, credentials and Wi-Fi details can
be typed on a phone instead of with a D-pad. Off afterwards, with a toggle.

**The parts that are right, and are recorded elsewhere rather than here:**
directional navigation as an architectural rule, the on-screen keyboard as a
properly designed screen rather than an afterthought, showing every platform the
server exposes and marking the unsupported ones, and USB as the input bootstrap.
Those are in Phases 3, 4 and 6 already.

**Why the companion page is deferred.** Three reasons, in order of weight.

1. **It is a fix for a pain nobody has felt yet.** The on-screen keyboard does
   not exist. Until it does, and until somebody has typed on it from a sofa,
   there is no measurement of how bad the problem is — only an assumption that
   it is bad. Build the thing that might make the companion unnecessary first.

2. **The problem is one hostname, not "a URL and credentials."** Cabinet's tvOS
   flow, read from the source: `ServerSetupView` takes the address and nothing
   else, then `PairingView` shows the approval URL **as a QR code** for the
   phone that is already signed in to RomM. The password is never typed on the
   television at all. CabinetOS should copy that flow, and when it does, the
   entire D-pad typing burden is a hostname, once. A whole second interface is a
   lot of machinery to save twenty characters.

3. **It cannot solve the case that would justify it.** Wi-Fi credentials are the
   genuinely painful typing, and a page served by the console is unreachable
   from a phone when the console is not on the network yet. Solving that needs
   the console to bring up its own access point — a large piece of work, and
   often impossible while the same radio is also meant to be a client. So the
   companion addresses the small problem and not the big one.

**If it is built anyway, the security surface is the part to get right**, and
"it is only a home LAN" is not a mitigation: a home LAN contains guests, IoT
devices and a television with an advertising SDK in it.

| | |
|---|---|
| **A one-time code shown on the screen**, carried in the QR and required by the page | The one that matters. Turns "anyone on the LAN" into "anyone who can see the television". |
| **Time-limited, not merely "off after first run"** | A setup abandoned half way otherwise leaves it listening forever. |
| **Write-only** | Never echo a stored credential back to the page. |
| **Guard the address field itself** | The real attack is not reading the page, it is pointing the console at an attacker's "RomM" and harvesting the token it then goes and fetches. |
| **Not HTTPS** | A self-signed certificate on a LAN teaches the user to click through certificate warnings, which is worse than the thing it fixes. |

**And the question the proposal already identifies is the right one to settle
first: rescue tool, or companion product?** The answer should be *rescue tool*,
and it should be enforced structurally rather than by intention — one page, no
navigation, time-limited, off by default. **If it ever grows a menu, it has
become a product**, with a second design language and a second set of bugs, and
nobody decided to build that.

*Do not resolve before the on-screen keyboard has been used on a television.*

### 15b. First run, and the one input that can be guaranteed
**Designed with MMagTech 2026-09-19, after the A9 Max was installed and the
setup was done by hand over SSH. Not built.**

Everything here exists because installing the first real console took a
keyboard, an SSH session and two commands nobody else would know. That is
fine for us and it is not a product.

#### The requirement, and everything else follows from it

> **A keyboard is needed exactly once, ever. After first run the console must
> never require one again, for anything.**

It is testable, which is why it is the requirement rather than a principle.

#### First run assumes a server, and there is no way past it

**MMagTech's call, 2026-09-19**, and it is what decides the shape of the whole
state machine:

> **You cannot have kept games until a server has been paired and you have kept
> one.**

So first run is a LINEAR PATH TO A PAIRED SERVER, not a branch. There is no
"set this up later", no "use it without a server", and no skip — because on the
other side of a skip there is nothing to show. A console that has never been
paired has no library, no covers, no saves, no user and no kept games; the only
honest thing it could offer is the stand-in demo library, and PROJECT.md
already says that must never appear on a console because it looks like a
working machine showing somebody else's games.

**That is a simplification, not a restriction.** Every step can assume the one
before it succeeded, and the last step can assume a real library exists to drop
the person into.

**AND IT DRAWS THE LINE UNDER OPEN QUESTION 22.** The offline console is a
strictly LATER state — a machine that HAS been paired and now cannot reach its
server — so it may assume it knows the user, the library it last saw and which
games are kept. "No server yet" and "no server right now" are different
problems, and only the second one has anything to work with. That is why the
offline design is not a first-run branch and must not be built as one.

#### Why the keyboard is the floor, and the controller is not

The instinct is to build setup around a wired controller — a console owner
owns controllers. **MMagTech's correction, and it is the right one: owning a
Bluetooth pad and having its cable to hand at setup time are different
things, and most pads sold now are Bluetooth.**

The keyboard guarantee is structural rather than lucky: **a machine cannot
reach an installed state without a keyboard, because the firmware boot menu
needs one.** That stays true after open question 5's work automates the
installer down to a single confirmation. So it is not "most people probably
have one" — it is "this machine could not exist without one having been
present."

| | |
|---|---|
| **Required** | a **keyboard**. Setup must complete with nothing else attached. |
| Wired controller | works if present; never assumed |
| Mouse | focus-move and click only — open question 16 |
| **Bluetooth controller** | **paired DURING setup, never a precondition for it** |

That last row is what the keyboard floor buys. Treating a paired controller as
a precondition creates a chicken-and-egg — pair a pad to reach the screen that
pairs pads — and with a keyboard underneath, pairing is simply another step
inside setup, driven in our own UI rather than by a Linux utility.

#### The chain, and why each link is gated

```
first run:  keyboard → network → RomM server → pair pad 1 → unplug the keyboard
later:      pad 1 → Settings → Add a controller → pads 2, 3, 4
never:      needing a keyboard again
```

**THE NETWORK LINK IS A HARD GATE, and it is the only one.** MMagTech,
2026-09-19: *the entirety of this OS relies on a RomM server*, so a console
that cannot reach a network cannot be set up and must not pretend otherwise.
One of Ethernet or Wi-Fi has to be working before setup can go on. There is no
"continue without a network", for the same reason there is no "continue
without a server" — on the far side of it there is nothing to show.

**Wi-Fi is offered even when Ethernet is already up**, skippable in that case
and required otherwise. It is the fallback for the cable being unplugged, and
first run is the one moment it can be configured with a keyboard to hand. See
open question 17, rung 1.

- **Wi-Fi before RomM**, because RomM is on the LAN. And the two networks are
  not the same question: open question 21 records that a LAN-only console has
  a complete library and three emulator systems that will *never* arrive,
  because those come from Flathub over the internet. **First run is where
  that gets said out loud**, not discovered by someone whose Switch games
  never appear.
- **Pairing RomM needs a second device.** The flow is device authorisation: it
  yields a URL and a user code and somebody must approve it in a browser. A
  console has no browser, so first run shows a **QR code** — built 2026-09-20,
  `frontend/src/qr.{h,cpp}`. **A browser signed in to RomM is therefore a real
  dependency of setup and is stated as one** — not a phone specifically, which
  is a tighter claim than the truth and would push a screen toward mentioning
  only phones. See *A browser is a dependency, and it is a safe one* below.
- **A physical keyboard types into the same field the on-screen keyboard
  shows.** One field, two ways to fill it; not two text-entry paths. And the
  on-screen keyboard does not become optional — first run is the one moment a
  real keyboard is near-certain, and changing a Wi-Fi password later from the
  sofa is not.
- **Pairing a controller is the last step and should be insistent.** Not a
  hard block, because the input model says a keyboard must keep working
  forever — but someone who skips it owns a games console they cannot play
  from the sofa, and Settings must offer it again.

#### Adding a second controller uses only the first

**MMagTech's requirement, and the one that makes the guarantee above real.**
A friend arrives with a pad; nobody should have to find a keyboard.

It also resolves an ambiguity in the rule Phase 6 already carries — *pair on
a button press, not on discovery; let the pad that sends the first input
become player one*. That is clean for the FIRST pad and under-specified for
the second: if the confirmation is "whichever pad sends input first", a
neighbour's pad in pairing mode can answer it.

**Two-sided confirmation, and it costs the user nothing:**

1. Pad 1 opens *Add a controller*; the console scans.
2. It shows what it found and **pad 1 chooses which**.
3. The new pad confirms by **sending its own first input**.

Neither half is sufficient alone. Pad 1 cannot adopt a stranger's device,
because that device never responds. A stranger's device cannot adopt itself,
because it was never selected. The confirmation is still the very input being
established, which is the original rule's insight applied at both ends.

#### Two things to build once, not twice

- **First run's pairing step and Settings → *Add a controller* are the same
  screen**, entered from two places. The frontend is already built this way:
  `--screen` opens a screen by walking the route a person walks, so a capture
  cannot show a state the product cannot reach.
- **The screen for "nothing is attached" must be readable and actionable with
  no input at all**, and should name what the console is currently being
  driven by. Phase 6 already demands three Bluetooth states rather than two —
  searching, found, and *no adapter, plug something in* — and this is the same
  rule one level up. A setup screen nobody can operate, with no explanation,
  is worse than a black one.
- **The escape hatch has to survive.** If pad 1's battery dies mid-session,
  USB always works and the keyboard must still work. That is the input model's
  existing line — supported, never a dependency — and this is the case that
  makes it load-bearing rather than polite.

#### What already exists

Most of the mechanism, which is why this is a screen problem rather than a
plumbing one: the on-screen keyboard, the device-authorisation pairing flow
and its code and URL, `/etc/cabinetos/session.env` as the file a first-run
screen writes, and `bluez` plus the MT7925's Bluetooth firmware in the image
with the adapter already naming itself `cabinetos` from the hostname.

#### THE FOUR MECHANISMS ARE BUILT — 2026-09-20

**What this bought, in one sentence: a console can now be asked where it is in
setup, and answer, on a machine with no screens built yet.** All four of the
things that were missing are in the tree, none of them draws anything, and each
one can be run from a shell against the reference console without disturbing
what is on the television. The screens still wait, like every other screen.

| | | |
|---|---|---|
| **A state machine** | `frontend/src/firstrun.{h,cpp}` | The chain, and every rule about what may be skipped |
| **A QR renderer** | `frontend/src/qr.{h,cpp}` | Byte mode, versions 1–10, error correction M |
| **NetworkManager plumbing** | `frontend/src/net.{h,cpp}` | Status, scan, join, forget, and the polkit verdict |
| **Knowing it is the first run** | `firstrun::completion()` | A marker, and the rule that adopts a machine set up by hand |

**And the polkit rule open question 17 asked for ships with them**, at
`system_files/usr/share/polkit-1/rules.d/60-cabinetos-network.rules`.

##### The state machine hands nothing to itself

`Machine` is given a `Facts` and judges it. It never calls the network, the disk
or a server — `observe()` is the one place that goes and looks, and it is
deliberately separate. That is the same split `screens::` makes, and it buys the
same thing: the whole flow can be walked at any point in it, on a machine with
no network, no server and no pad.

**Which is what made the rules testable, and the test found a real deadlock.**
`--first-run-rules` walks every combination of facts the machine can be handed —
96 of them — and asserts the refusals rather than the happy path: that setup
cannot finish while offline, with no server answering, or with no token; that
Wi-Fi cannot be passed over while offline; that every Blocked step has a
sentence; and that `advance()` and `skip()` each refuse anything but their own
gate. It runs in CI and needs nothing.

The deadlock it found: **the Wi-Fi step's skip was keyed on ETHERNET being up
rather than on being ONLINE.** Those coincide only while this console knows
about exactly two kinds of link. The day `net.cpp` counts a third, a machine
online over it would pass the network gate and then sit at a Wi-Fi step it could
neither satisfy nor skip — setup stuck, on a machine that is on the network.
Reading the code again would not have found it; walking every combination did.

##### Knowing it is the first run — and the machine set up by hand

The hard half of this question is not the marker file, it is **the reference
console**. Somebody SSHed into it and wrote `session.env` and a token, which is
exactly what this flow will one day produce. It has never seen a setup screen
and must never be shown one: a console that boots into a wizard after a year of
use is a far worse failure than one that skips a wizard it did not need.

So the answer is in two parts and the second matters more:

1. A marker at `<root>/config/first-run.json` says setup finished.
2. **With no marker, a machine that already has everything setup produces — a
   server address, a token, and a user behind that token — is taken as set up**,
   and the marker is back-filled recording that it was adopted rather than
   walked.

Rule 2 is the difference between *"has this flow been run"* and *"is this
machine configured"*, and only the second is the question anybody cares about.
**Measured on the A9 on 2026-09-20**: `--first-run` reports *"not needed — this
machine is already configured"* and walks the chain to completion.

##### The server address now has somewhere to be written

First run's one lasting output is the RomM address, and the session user
**cannot write `/etc/cabinetos/session.env`** — it is root's. Rather than invent
a privileged helper for one string that is not a secret, first run writes
`<root>/config/server.json`, and the resolution order is:

```
--romm  →  $CABINETOS_ROMM  →  /etc/cabinetos/session.env  →  config/server.json
```

**Root's answer wins**, because `session.env` is the documented way to set a
console up by hand and a file the session wrote must not silently override it.
Writing the address while something that outranks it says otherwise is reported
rather than done silently.

**`session.env` is read DIRECTLY as well as through the environment**, and that
is not redundancy. The session script exports it, so a frontend started by the
session sees it either way — but one started over SSH does not, and that is how
every check of this gets made. Without it, `--first-run` on the reference
console reported *"NEEDED"* on a machine that had been working for a day. **An
instrument that lies about the thing it exists to report is worse than no
instrument.**

##### The QR encoder is written out, and it was checked against two others

Byte mode, versions 1 to 10, error correction M — roughly four times the
headroom a RomM verification URL needs. Longer input is refused rather than
silently truncated. It has no dependency, because the alternative is a library
in the image and in the build container for one screen that runs once in a
console's life.

**A QR code cannot be checked by looking at it**, which is the whole difficulty:
a wrong one looks exactly like a right one. So it was checked three ways on
2026-09-20, and each way found something:

- **Module-for-module against two independent encoders.** With the mask forced,
  every case from version 1 to version 10 matches exactly. This found three real
  faults: a BCH remainder that **tested the wrong bit** and therefore put no
  error correction in the format field at all; two timing-pattern modules
  blanked by the format-area reservation; and a mask-penalty rule that treated
  off-the-edge as light and scored every real finder pattern twice.
- **A third implementation broke a tie.** The two references disagreed about
  padding — one writes a longer, legal, non-minimal terminator. A third agreed
  with ours byte for byte, including every error-correction codeword.
- **Round-tripped through a real decoder.** 18 of 19 cases decode back to the
  exact input. The nineteenth produces a matrix **byte-identical to a reference
  encoder's**, and the reference fails to decode in precisely the same way at
  precisely the same scales — so it is the detector, not the encoder.

**THE QUIET ZONE IS THE RENDERER'S JOB AND IT IS NOT OPTIONAL.** Measured: the
same code drawn flush to the edge does not decode at all, while with four
modules of margin it decodes every time. It is the commonest reason a perfectly
correct code will not scan, and it is margin — which is layout, which belongs to
whatever draws this.

**The mask is a legitimate difference between encoders.** Three implementations
picked three different masks for the same string and all three are valid; the
format field records which was used. So "matches a reference exactly" is not
achievable across implementations, and the decode test is what settles it.

##### nmcli, and not libnm or D-Bus

`net.cpp` shells out to `nmcli`. That looks like the lazy answer and it is the
considered one: libnm wants a GMainLoop and this program already has a frame
loop that owns the process; raw sd-bus avoids the loop but not the work, since
adding a Wi-Fi connection means hand-building NetworkManager's nested
`a{sa{sv}}` settings schema, which is the part most likely to be subtly wrong
and the part with no way to check by hand. **nmcli is already in the image, and
any fault in it can be reproduced at a shell in one line** — which on a project
whose rule is *measure rather than reason* is worth more than elegance.

The price is paid in two places and both are written down:

- **Terse output is colon-separated with backslash escapes, and an SSID may
  legally contain a colon.** Anyone within radio range picks their own SSID, so
  a naive `split(':')` is not a tidiness bug — it is a stranger deciding how many
  fields this console thinks it received.
- **Nothing goes through a shell.** Every command is `fork`/`execvp` with an
  argv array. A scan puts unvetted bytes from strangers into this process every
  time it runs.

One exposure is kept rather than solved, and recorded: a passphrase passed to
`nmcli` is visible in that process's argv while it runs. On this machine that is
not an escalation — the only readers are the same user and root, and
NetworkManager stores the passphrase where root can read it anyway.

#### AND THE SCREENS ARE BUILT TOO — 2026-09-20

**`frontend/src/setup.{h,cpp}`.** Five screens, one shape: prose on the left,
the thing you act on in a panel on the right, actions along the bottom. The step
changes what is in the panel and nothing else, so the flow does not read as five
unrelated screens.

**IT RUNS A LOOP OF ITS OWN rather than being a mode inside the main one.** The
library is fetched from RomM before the main loop exists, so a first run woven
into that loop would have to survive a state where the thing the loop is built
around does not exist. And it genuinely is linear and happens once — a mode flag
would be modelling a freedom the product does not have.

**NOTHING IN IT BLOCKS THE FRAME.** A Wi-Fi scan is seconds, a Bluetooth scan is
ten, and waiting for somebody to pick up a phone is minutes. Every one runs on a
worker and is polled once a frame, the same shape `LaunchJob` and `StateLoad`
already use. A setup screen that froze while looking for networks would be
indistinguishable from a console that had crashed — and it would be the first
thing anybody ever saw it do.

**The step's own sentence comes from `firstrun::Machine::because()`**, so the
words a person reads and the rule the console is enforcing cannot drift apart.

##### The QR is proved all the way to the glass

Encoding correctly is not the same as drawing correctly. The code is uploaded as
a single-channel texture with **nearest filtering** — a QR is the one thing on
this console that must not be smoothed — and drawn as one quad on a white card
carrying the quiet zone as real light modules.

**Measured 2026-09-20, and this is the test that matters**: a capture of the
finished 3840x2160 frame, taken off the A9's own Radeon, was handed to a decoder
with no cropping and no help, exactly as a phone pointed at the television sees
it. It read back the live pairing URL the server had issued seconds earlier.
Server → encoder → GL texture → framebuffer → decoder, end to end.

##### The setup loop is paced at sixty, and that is not tidiness

A static page of text left unpaced runs as fast as the GPU will go — thousands
of frames a second on the A9, spinning a discrete graphics chip to draw a list,
on a machine that may be in a cabinet.

**And it is what makes `--frames` mean anything.** Every one of these screens is
waiting on something that takes seconds, so a capture has to be able to wait in
units a person can reason about. Unpaced, four hundred frames on the A9 went by
before the server had answered and the capture of the pairing screen came out
with no code on it — **the same trap `--launch-after` fell into, one screen
along.**

##### The copy assumes a competent adult, and that is a rule

The first draft explained how to pair a controller. MMagTech, 2026-09-20:

> **if you have a RomM server and can install an OS I shouldn't need to tell you
> in depth how to pair a controller**

That is a better test than *is this clear*, because it is about the reader
rather than the sentence. **The person in front of this screen has already stood
up a self-hosted web application and written an operating system to a USB
stick.** Explaining what a pairing button is insults them, and it buries the one
thing they do need — what this step will and will not let them do.

So every line says the CONSTRAINT and stops. Required or optional, and why only
when the why is not obvious:

| | |
|---|---|
| **Connect to Network** | *A network connection is required.* / *Connected over Ethernet.* |
| **Set up Wi-Fi** | *Optional. A fallback for when the cable is unplugged.* |
| **RomM Server** | *Enter the address of your RomM server.* |
| **Pair with RomM** | *Approve this console in a browser signed in to RomM.* |
| **Pair a Controller** | *Optional, and you can add one later in Settings.* |
| **Ready** | *You can unplug the keyboard. You will not need it again.* |

The last one is the only line worth spending, because it is the promise the
whole flow was built to keep and nothing else on the screen says it.

**The titles are what the step DOES**, not a greeting. "Connect to Network", not
"Let's get you online".

##### And three more that only pulling the cable out could find

**MMagTech unplugged the Ethernet on a console whose Wi-Fi profile had been
deleted, so the machine was genuinely offline — the state neither machine here
can otherwise reach.** It found the network step's whole reason for existing
broken in three different ways.

**THE SCREEN WAS A PHOTOGRAPH.** The facts were read when a step was entered and
never again, so somebody sitting on *"A network connection is required"* who
plugs a cable in watches nothing happen, forever. That is the exact moment this
screen exists for. The link is now re-read every two seconds on a worker —
a worker because `net::status()` is three or four nmcli round trips, several
hundred milliseconds, which is ten frames.

**NOTHING EVER STARTED A SCAN.** The scan was kicked off by ARRIVING at the
step, and the step was entered while the machine was still on a cable — so no
list was needed and none was asked for. Then the cable came out, the panel
correctly switched to showing a Wi-Fi list, and the list it showed was the empty
one nobody had ever filled: *"Nothing on the air"*, in a house with four
networks in it, with the only remedy a button somebody had to know to press.
**A scan is now started by what the screen NEEDS, not by how somebody got
there**, and retried on its own.

**A REFUSED RESCAN LOOKED LIKE AN EMPTY SKY.** NetworkManager declines
`--rescan yes` while a scan it started itself is running, and the two-second
status poll above makes that collision more likely — so the fix partly caused
the fault. It now falls back to the cached list, which NetworkManager keeps
current anyway, and *"Nothing on the air"* is told apart from *"Could not
scan"*: they mean different things and only one of them means try again.

**Measured afterwards, with both Ethernet devices reporting `unavailable`:**
joined in about thirty seconds including typing the password,
`MMagTech.nmconnection` written root-owned 0600, autoconnect on, running on
`192.168.1.109/24` over the radio.

##### Three faults only walking it could find — 2026-09-20

MMagTech walked the flow on the television. None of these was visible in a
capture, and two of them are about what the console SAYS rather than what it
draws.

**THE LAST SCREEN PROMISED SOMETHING THAT WAS NOT TRUE, and it was the one
promise the whole design exists to make.** The controller step says pairing is
optional; the last screen said *"You can unplug the keyboard. You will not need
it again."* Somebody who skips the controller step has exactly one input, and
the console was telling them to unplug it.

> *"on the bluetooth pairing screen it said it wasn't required and could be done
> later, but then on the last screen said the keyboard could be unplugged and
> wasn't needed anymore"*

So the promise is now made only when it is true, and the controller step says
what skipping costs in the same breath as saying it is allowed — *"Optional, but
without one you will still need the keyboard."* **A guarantee stated
unconditionally by a flow that can be skipped is not a guarantee, it is a bug
with good intentions.**

**The network step showed the Wi-Fi list it did not need.** On a cable, the same
list appeared twice in a row — once under *Connected over Ethernet*, where it
was irrelevant, and again on the Wi-Fi step where it belongs. Two screens that
look the same read as the flow having gone backwards. The network step now shows
the link and its address, and the list appears there only when there is no other
way forward.

**The last screen drew an empty panel.** No rows, so a grey box sat beside
*Start playing* — which reads as a list that failed to load, on the one screen
whose whole job is to say that everything worked.

##### The fault the captures could not find, and the television did

**Every screen captured correctly and the console drew nothing.**

`Renderer::beginFrame` binds an offscreen scene target so that panels can blur
what is behind them, and `presentScene()` is what puts that texture on the real
framebuffer. The setup loop never called it. So it ran at sixty frames a second,
presenting nothing, at 5% of a core, with no error anywhere — the television
showed white and gamescope's own screenshot came back entirely black, and
neither of those is a message anybody can act on.

**It was invisible to every capture**, because `--render-size` takes the
offscreen path and `saveFrame` reads that target directly. Every screenshot in
this section was taken through a code path the console does not use.

**So an offscreen capture does not prove a window ever gets a frame**, and that
is a limit of the instrument this project has leaned on for a fortnight. Found
2026-09-20 by MMagTech, on the television, in the first ten seconds of looking
at it.

##### Four faults the captures found, and none was visible in the code

- **Focus landed on rows that do nothing.** Every placeholder this flow draws —
  *Looking for networks…*, *This console has no Bluetooth* — is disabled, so
  this was the common case rather than an edge one. A focus rim on a row that
  ignores the button is the worst thing a setup screen can do, because there is
  no way to tell it from a crash.
- **It said "Nothing answered at that address" before it had tried.** Arriving
  at the server step with an address already in `session.env` is the COMMON
  case. Fixed at the rules level with a `serverChecked` fact, because "we asked
  and got nothing" and "we have not asked" are different sentences and the
  machine could not tell them apart.
- **The pairing URL ran off its column and under the panel.** It is one
  unbroken token and the longest string the flow ever draws, so word wrapping
  could not touch it. There is now a codepoint-wise hard wrap — UTF-8 aware,
  because a break inside a multi-byte character produces a glyph the font
  cannot resolve.
- **BLUEZ USES THE ADDRESS AS THE NAME when a device has not given one**, with
  dashes where the address has colons. So an unnamed device does not have an
  empty name, it has a name that looks like one — and the list somebody picks
  their controller out of filled with **sixteen** of the neighbours' beacons.
  Unnamed devices are now counted and hidden behind a button rather than
  dropped, because a pad bluez has not resolved yet is exactly the thing
  somebody has just woken up.

##### What is still owed here

- **`join()` has not been run against a real access point.** Status, scanning,
  the polkit verdict, the whole state machine and every screen are measured on
  the A9; actually joining a network is not, because the reference console is
  on a cable and taking it off is how you lose the machine you are measuring.
  **Do this with a keyboard at the console, not over SSH.**
- **Nobody has walked the flow with their hands.** Every screen is captured and
  every mechanism is measured, but the whole of it start to finish, on a
  television, with a keyboard, has not been done — and cannot be on either
  machine here without unconfiguring one of them.
- **The look is a first pass.** It is consistent and it is legible at ten feet,
  but it has not been judged on the 65-inch by a person.

#### A browser is a dependency, and it is a safe one — decided 2026-09-20

Pairing is a device authorisation: somebody must approve it in a browser that is
already signed in to RomM, and the console has no browser. This was going to be
recorded as an open question. **MMagTech closed it, and the reasoning is
stronger than the argument for the keyboard:**

> if you have a mini PC lying around to install this on, you have a phone as
> well, or another browser

**You cannot have a RomM library without a browser.** RomM is a self-hosted web
application; to be at the point of setting up a console you have already stood
up a server, signed in to it and added games — all through a browser. So this is
not a hopeful guess about the user, it is a restatement of the thing first run
is pairing *with*. The keyboard is justified by the hardware; this is justified
by the premise, which is firmer ground.

Two things follow, and neither costs anything:

- **Say "phone or computer", never "phone".** The QR only saves somebody typing
  the URL; a laptop already signed in to RomM is the easier path for plenty of
  people. **The screen shows the URL and the short code as text beside the
  code**, so either works.
- **A failed pairing says what was observed and nothing more.** The step already
  needs a timeout, because a person can simply walk away — *"RomM didn't confirm
  this console. Here's the link and code again."* A console that starts
  diagnosing somebody's reverse proxy has lost the plot; their infrastructure is
  not ours to explain, and we do not claim to know why.

#### How to see any of it work

```
cabinetos-frontend --first-run                 where setup is, and where it would stop
cabinetos-frontend --first-run-check-server    the same, and ask whether the server answers
cabinetos-frontend --first-run-step wifi       open the chain at one step
cabinetos-frontend --first-run-rules           96 fact combinations, asserting the refusals
cabinetos-frontend --network                   link, radio, and the polkit verdict
cabinetos-frontend --network-scan              the same, and what is on the air
cabinetos-frontend --qr "<text>"               a code on a terminal, scannable off the screen
```

All of them run before SDL, so they need no window, no GL and no controller, and
**none of them disturbs the session on the television**.

**`--first-run` and `--network` both report WHO IS ASKING**, because the polkit
grant depends on the session the caller is in: over SSH the verdict is
`auth_admin_keep` and at the console it is `yes`, and both are correct. A probe
that printed one number without saying which question it put would be the third
lying instrument this project has had to fix.

### 16. Is a mouse supported, or not?
**Raised 2026-09-13. Two documents currently disagree. Needs a decision, not a
default.**

*The input model* in this document says: **Supported — keyboard and mouse. A
convenience, never a dependency.**

The first-run design proposal says the opposite, and gives a good reason:
*"pointer input pulls the design toward hover states and click targets, which is
a different interaction model."* That is true, and it is the reason tvOS has no
pointer.

Both cannot stand. The honest options:

1. **Mouse is not supported.** Amend the input-model table. Cleanest, and it
   matches how the design is actually being built.
2. **A mouse moves focus and clicks the focused thing, and nothing else** — no
   hover states, no pointer, no cursor. Cheap to keep true, and it means a
   plugged-in mouse does something sensible rather than nothing.

What must not happen is leaving the table saying "supported" while no screen is
built for it, because that is a promise the product does not keep.

**Keyboard is not in question** and is settled: every screen must be fully
operable by directional input plus confirm and back, from whatever device
supplies them. That is already an architectural rule rather than a feature.

**ANSWERED 2026-09-19: option 2, and the reason is that the mouse stopped
being load-bearing.** Working out first run (below) established that a
*keyboard* is the guaranteed input on any installed machine, not a mouse and
not a controller. Everything a person must do can therefore be done with
directional input, confirm and back — which the architecture already
requires. That leaves the mouse with nothing it uniquely enables, so it gets
the cheap treatment: **it moves focus and clicks the focused thing, and
nothing else.** No cursor, no hover states, no pointer affordances, and
therefore no second interaction model to design for or test.

The first-run proposal's objection stands and is respected rather than
overruled — pointer input *would* pull the design toward hover and click
targets, which is why none of that is built. A mouse under this rule is not a
pointer; it is a second way to drive the one model that exists. Amend the
input-model table to say exactly that, rather than the bare word "supported".

### 17. Wi-Fi credentials on a controller-only console
**Raised 2026-09-13. The mechanism is DECIDED; the UI is Phase 6.**

The hard case in first-run setup, and the one the companion page (open question
15) cannot solve: a page served by the console is unreachable from a phone when
the console is not on the network yet.

#### The permissions question, answered first, because it is the blocker

Checked on the running image. **The frontend can do the entire Wi-Fi flow
without a password prompt**, which a console has no way to answer anyway:

| polkit action | `allow_active` | needed for |
|---|---|---|
| `wifi.scan` | **yes** | listing networks |
| `network-control` | **yes** | activating a connection |
| `enable-disable-wifi` | **yes** | turning the radio on |
| `settings.modify.own` | **yes** | saving a user-scoped connection |
| `settings.modify.system` | `auth_admin_keep` — **but see below** | saving a connection that comes up at boot |

The system-wide case is granted by a rule Bazzite already ships:

```js
if (action.id == "org.freedesktop.NetworkManager.settings.modify.system" &&
    subject.isInGroup("wheel") && subject.local) return polkit.Result.YES;
```

**The session user is in `wheel`, so this works today — by inheritance, not by
design.** `wheel` also means sudo, and Phase 6 tightening security around
developer mode is exactly the change that would remove the session user from it
— **silently breaking Wi-Fi configuration**, presenting as a network bug rather
than a permissions one. Same class of trap as the controller ACL in *Controls*.

**So CabinetOS should ship its own polkit rule** granting that one action to the
session user directly, rather than depending on `wheel`. Cheap, and it decouples
"can configure the network" from "can become root", which are not the same
privilege and should not be the same grant.

**SHIPPED 2026-09-20**, at
`system_files/usr/share/polkit-1/rules.d/60-cabinetos-network.rules`. It names
one action and one user and adds nothing else, so the session user can be taken
out of `wheel` whenever Phase 6 wants to and Wi-Fi keeps working.

**AND IT WAS MEASURED ON THE A9 RATHER THAN REASONED ABOUT**, which mattered,
because the obvious check proves nothing: the machine already answers `yes` by
way of `wheel`, so installing a rule that also says yes changes no observable
thing. The decisive test was to install the rule **returning `NO`** and watch
the verdict flip:

```
pkcheck --action-id org.freedesktop.NetworkManager.settings.modify.system         --process <the frontend's pid>

   with nothing of ours installed   polkit.result=yes      (Bazzite's wheel rule)
   with ours installed saying NO    polkit.result=no       (ours is consulted first)
   with ours installed as shipped   polkit.result=yes
   ours removed again               polkit.result=yes
```

The second line is the whole proof. **`60-` sorts before
`org.freedesktop.NetworkManager.rules`**, polkit takes the first rule that
returns a result, and ours therefore answers whether or not Bazzite's file is
present or still grants anything. The machine was put back as found.

**`subject.local` is kept and it is not ceremony**: the grant belongs to
somebody sitting at the machine, not to an SSH session. Developer mode hands out
SSH deliberately (open question 9), and a shell reached over the network should
not silently inherit the console's own privileges. The visible consequence is
that `--network` reports `auth_admin_keep` over SSH and `yes` at the console,
and **both are correct** — which is why that probe says which question it put.

**The console can now ask this question itself.** `net::polkitVerdict()` reports
what polkit says about saving a system connection, and `join()` appends it to
the error when a join fails for want of privilege. That turns the Phase 6 trap
from a silent Wi-Fi failure on a machine whose network is fine into one line
naming the rule file.

#### The ladder, in the order the UI should offer it

1. **Ethernet, which needs no typing** — but it does NOT skip the Wi-Fi step.
   **Reversed by MMagTech 2026-09-19**, and the reversal is right.

   This used to say "skip the screen entirely when it is already up: do not ask
   someone to confirm a network they are already on." **Wi-Fi is not a
   duplicate of the cable, it is the fallback for losing it** — and a console
   under a television is exactly where a cable gets tripped over, moved house
   or pulled out to borrow. This whole OS is useless without reaching RomM, so
   a machine whose only path to the server is one cable is a machine one
   accident away from being a brick.

   **And setup is the one moment the fallback can be configured cheaply.** This
   document already makes that argument, against itself, two sections down: the
   on-screen keyboard cannot become optional because *"first run is the one
   moment a real keyboard is near-certain, and changing a Wi-Fi password later
   from the sofa is not."* That is an argument for asking while the keyboard is
   still plugged in, not for skipping.

   So: **with Ethernet up, the Wi-Fi step is offered and SKIPPABLE** — the
   person is already online and is choosing whether to set up a fallback. With
   no cable, it is not skippable, because it is the only way forward.
2. **The on-screen keyboard.** The baseline, and **this is what every console
   does** — PlayStation, Xbox, Apple TV and Switch all make you type the
   passphrase with a controller. It is not a product failure, it is the normal
   experience, and the keyboard has to exist for the RomM address regardless.
3. **A USB keyboard.** Thirty seconds, and people have one in a drawer. The
   input model already requires that one works wherever text is entered, so this
   costs nothing beyond saying it on screen.
4. **A phone on a USB cable.** Enable tethering and the console is online
   immediately — NetworkManager picks it up as an ordinary ethernet device with
   no configuration at all.

   **NOT NAMED ON THE SCREEN, reversed 2026-09-20.** This used to say it was
   worth naming because nobody thinks of it. MMagTech's objection, and it is the
   right one: *"i dont know if i like the idea of the phone option, leaves a lot
   of potential on me when this doesn't work for people."*

   Checking the image settles it. **Android tethering is pure kernel** —
   `rndis_host` and `cdc_ncm` ship in the image and the phone simply appears as
   a wired device. **iPhone tethering needs `usbmuxd`**, which is installed but
   `inactive` and `static`, and it needs the phone to TRUST the computer: a
   prompt, an unlock and a pairing step, none of which has ever been run on this
   console.

   So it is a promise that holds for one phone ecosystem and is untested for the
   other — offered on the one screen where somebody is already stuck and out of
   options. **A console should not suggest a fix it has never seen work.** It
   stays here as a trick for whoever is setting a machine up; it does not go on
   a television until somebody has done it and it worked.

#### Against the console running its own access point

The "proper" IoT answer — broadcast a setup network, have the phone join it,
serve the page there. **Decided against for now**, on four counts:

- **Hardware-dependent.** Plenty of cheap combo cards do AP mode badly or not at
  all, and this project's hardware rule is that capability is discovered rather
  than assumed.
- **It is a mode switch, not an addition.** While acting as an access point the
  radio generally cannot also scan for the network it is meant to join.
- **The phone loses internet** while attached to it, which reliably confuses
  people and makes them abandon the flow.
- `wifi.share.open` is **denied** even to an active session in this image, so it
  needs a polkit change too.

Revisit only if the on-screen keyboard turns out to be genuinely unusable on a
television, which is a thing to find out rather than assume.

#### Three decisions that matter more than the mechanism

1. **Show the password by default.** Every console hides it and every console is
   wrong: there is nobody shoulder-surfing a living room, and not being able to
   see what you typed *is* the entire difficulty. This one choice removes most
   of the pain for free.
2. **Skip the screen when already online.**
3. **Remember networks.** NetworkManager does this for nothing.

And three details that will otherwise be discovered late: **hidden SSIDs** need
a "join another network" path where the name is typed too; **the passphrase is
usually being read off the underside of a router**, so digits and symbols must
not be buried behind a shift layer; and **802.1X enterprise is a different form
entirely** — out of scope, and better refused plainly than half-supported.

### 18. The on-disk layout
**Raised by MMagTech 2026-09-17. DECIDED the same day. BUILT 2026-09-18, and
the test VM was moved onto it. The last undone piece — PPSSPP's system files
shipping inside the image at `/usr/share/cabinetos/system/` — landed
2026-09-19 with Phase 5's deploy.**

> *"What I don't want is for these to be in completely random places throughout
> the OS. If we had to ssh or sftp into the file system there should be an
> organized folder structure that makes it distinct and easy to find."*

A fair description of what exists. **The current layout was never designed — it
accumulated**, and three of its problems were hit in one afternoon while adding
PSP saves:

- **There are two save directories.** `romcache/saves/` when a game is launched
  from the library, and `saves/` when it is launched with `--core`. PSP save
  folders were found in both. A save written one way is not seen the other way.
- **`system/` holds three unrelated kinds of thing**: BIOS fetched from RomM
  (replaceable), a Dreamcast's saved console settings (irreplaceable), and now
  13 MB of PPSSPP fonts that are part of a build's output. This document already
  carries a warning not to clean that directory. A warning is standing in for a
  layout.
- **Every core shares one flat save directory**, which is why attributing a PSP
  save folder to the game that wrote it needed mtime comparison rather than a
  path.
- **`romcache/` is named "cache" and holds things that are not a cache** — kept
  games, pending uploads, and every save. Cabinet renamed its Storage screen the
  moment it held things that were not a cache; the directory here has the same
  lie in its name.

#### Take the vocabulary that already exists

**Read from RetroArch's and RetroBat's own documentation rather than recalled.**
Both use top-level folders named after what is in them — `bios`, `roms`,
`saves`, `states`, `screenshots`, `cheats`, `config` — with no nesting of
unrelated things. **Someone who has used either already knows where to look,
which is most of what this question is asking for.**

Three specifics worth taking:

1. **Saves and states are separate top-level directories.** This console has no
   states directory at all today; states go to RomM and nowhere else.
2. **RetroArch sorts saves into folders by core name, and it is ON by default.**
   So the per-core split is not an invention, it is what the most-used frontend
   in this space already does. It also offers sorting by content directory.
3. **RetroBat separates user data from program data** — a `user/` folder that
   survives updates. That maps exactly onto a bootc image, where `/usr` is
   replaced wholesale on every update and `/var` is the person's.

#### The cache is the one thing that is genuinely ours

Both of those assume the games are yours and permanent: `roms/` *is* the
library. This console pulls on demand, so it has two categories neither needs —
a game that is here because it was played, and a game that is here because
somebody asked for it. **MMagTech's own read, and it is right.**

Keeping them apart at the top level has a payoff beyond tidiness: **eviction
only ever deletes inside `cache/`**, which is a rule that can be checked by
looking rather than by reading code, and it lines up with the drive split
already decided in open question 14 — kept games travel, the cache does not.

#### Per-user from the start, because RomM already is

**Raised by MMagTech: RomM has users, tvOS switches between them, so the layout
should account for it before it exists.** It should, and RomM has already
designed the answer. Read off the live server:

```
users/557365723a31/saves/Sony Playstation/356/pcsx-rearmed-native/<file>
```

`557365723a31` is hex for **`User:1`**. So RomM namespaces by **user first**,
then asset kind, then platform, then rom id, then emulator tag. Mirroring that
locally means the tree on the console and the tree on the server are the same
shape — sync becomes obvious and a fault is visible by eye. (The same listing
shows a save whose path simply *ends* at the rom id, with no emulator segment:
that is the untagged PlayStation card, and the missing tag is a missing
directory level.)

**What is NOT per-user matters as much as what is.** Two people on one console
must not download the same game twice, or hold two copies of the PS2 BIOS — and
must never see each other's saves.

| Per user | Shared by the machine |
|---|---|
| saves, save states | the downloaded game files |
| screenshots | BIOS and firmware |
| preferences | cores |
| **the decision** to keep a game | shader caches |

That last row is the subtle one and it belongs to the account-switching session
rather than this one: the FILE is shared, the KEEP is personal, so releasing one
person's keep must not delete a game somebody else pinned. The layout only has
to leave room for it.

#### The shape

```
<storage location>/
├── roms/      kept games                shared
├── cache/     pulled games              shared, and the only thing eviction touches
├── bios/      firmware from RomM        shared
├── users/
│   └── <id> - <name>/
│       ├── saves/<platform>/<romId>/<core>/
│       ├── states/<platform>/<romId>/<core>/
│       ├── screenshots/
│       └── config/
├── config/
└── logs/
```

The root is `/var/lib/cabinetos/` on the internal disk. **`roms/` and `cache/`
repeat on every storage location** rather than living only at the root — see
*One kept game, two people* below, which is the reason.

**The root is created at every boot by
`system_files/usr/lib/tmpfiles.d/cabinetos.conf`, not by the image**, and the
distinction is load-bearing: bootc unpacks `/var` from the initial image only,
so a directory made at build time would exist on machines installed after it
and never on machines that upgraded into it. Phase 5, *The deploy*, has the
detail. Without it `storage::root()` falls back to the working directory, which
for a systemd service is `/`.

Anything that ships inside the image — PPSSPP's fonts and lookup tables — lives
in `/usr/share/cabinetos/system/` and is never written to. `ensureTree`
symlinks what it finds there into the console's system directory at startup,
leaving alone any name that is already a real file, which is what keeps a
development machine working.

#### One convention, twice: the number identifies, the words are for you

`users/1 - MMagTech/`, and `roms/Sony Playstation/321 - Crash Bandicoot.chd`.

**REVISED 2026-09-18, while building it: one spelling of a platform, not two.**
The line above once read `roms/psx/`, and the saves below already used RomM's
`fs_slug` because mirroring the server was the whole argument for the per-user
tree. Two spellings for one console is exactly the thing this question exists to
stop, and MMagTech said so on sight. **The short `slug` is also not unique** —
the reference server has two Arcade platforms sharing `arcade`, 223 games
between them, needing different cores — so under it those games share one folder
while their saves correctly split into `FBNEO/` and `MAME2003/`. `fs_slug` wins
on both counts, and RomM uses it for its own roms as well as its assets, so the
two trees are the same shape all the way down. The cost is spaces and mixed case
in a folder name, which the user directory already accepted; checked against the
live server, none of its 36 `fs_slug`s collide case-insensitively.

**The username alone was considered and is not the key.** It is available —
`/api/users/me` returns it — and it is what a person recognises, so it belongs
in the name. But **usernames change and ids do not**: rename in RomM and a
name-keyed console would quietly create an empty folder and start again, with
every save still on disk and nothing looking for it. That is the same failure as
the untagged save above — the data is fine, the label moved. Two smaller
reasons: a username can hold spaces and unicode, and `Matt` and `matt` are one
directory on a FAT or exFAT drive, which is exactly the USB case open question
14 contemplates. **RomM itself hex-encodes `User:1` rather than using the name**,
which suggests the same conclusion reached independently.

So the leading number is matched and everything after `" - "` is decoration that
may be re-derived at any time. A rename becomes cosmetic rather than
destructive.

#### One kept game, two people

**MMagTech, 2026-09-17: if one person keeps a game and somebody else plays it
without keeping it, what happens on disk?** The answer is short and the
consequences are not.

**One copy. The second person just plays it.** Nothing is downloaded, nothing is
copied, and nothing lands in their cache — the file is already on the machine
and it stays the first person's kept game. Two people on one console never hold
two copies of a 1.8 GB game.

Three things follow:

**1. "Kept" stops being a flag and becomes a set of people.** If both keep it and
one releases, it must remain kept for the other. Today `cache::keep`,
`unkeep` and `isKept` are a boolean per rom id with no notion of who — **that is
the one piece of existing code this answer changes.**

**2. Releasing the last keep DEMOTES rather than deletes.** The game becomes an
ordinary cached file: still playable, now evictable, and re-keeping costs
nothing because the bytes never moved. That makes un-keep a safe button rather
than a destructive one, which matters when it sits one press away on a game's
own screen.

> **REVERSED 2026-09-19: releasing the last keep DELETES.** MMagTech, on being
> shown the behaviour: *"most users would assume unkeeping a chosen game would
> free up space on their drive."* They would, and there is a sharper version of
> the point — **the row says "Remove download" and it removed nothing.**
>
> The safety argument above does not survive contact with what it is protecting.
> Every game here is a copy of RomM, so the worst a mis-press costs is a
> download this console is built to make invisible; that is a very small thing
> to buy with a button that appears to do nothing. Deleting matches the words on
> the row, matches every other console, and matches why anybody presses it.
>
> **Two callers still demote**, and neither is somebody asking for space: the
> game being played right now, whose files the core has open, and a keep whose
> download failed — which is undoing a promise, and where the file may be a
> perfectly good game that was already on the disk before Download was pressed.
>
> **And the delete calls `syncfs`.** Without it btrfs reports the old free-space
> figure until a transaction commits, so a Storage screen refreshed a second
> later would show no change at all — which is the exact complaint that started
> this. Measured on the games disk first, to be sure the fault was real rather
> than assumed: write 228 MB and `avail` drops by exactly that; `rm` it and
> **`avail` does not move at all**; `sync` and the whole 228 MB returns.
>
> **MEASURED END TO END, 2026-09-19.** Keep Hammerin' Hero, then press Remove
> download and read the free space immediately, with no manual sync anywhere:
>
> | | |
> |---|---|
> | Free with the game kept | 67,607,900 KB |
> | Free straight after the release | 67,830,524 KB |
> | Handed back | **217 MB, which is the whole game** |
>
> And the two-keeper case still holds: with a second person keeping it,
> *"released by user 1, still kept by 1 other(s)"* and the file does not move.

**3. And that is why `roms/` and `cache/` repeat per drive.** Kept games live on
the large drive and the cache on the internal one, so a demotion at the ROOT
level would mean physically copying gigabytes between disks because somebody
changed their mind. With the same shape on every location, demotion is a rename
inside one filesystem — instant — and *"eviction only ever touches `cache/`"*
stays true on both drives instead of becoming a rule with an exception.

Two smaller consequences, recorded so they are not rediscovered:

- **Last-played is the MACHINE's, not a person's.** Eviction takes the least
  recently played, and reading that per user would evict a game because *you*
  have not touched it while somebody else plays it daily. For a shared file it
  is the most recent play across everyone on the console. Play history itself
  stays RomM's and per-user; this is a separate, local fact about a file.
- **Playing a kept game does not quietly keep it for the player.** Keeping stays
  a deliberate act, which is the entire distinction between the two categories.

#### Why now, and the one caveat

**Now it costs nothing** — one user, one directory — and later it means moving
every save on every machine. Saves are the only category of data on this console
that cannot be re-downloaded, so that asymmetry decides it.

**The honest caveat:** the real key is *(server, user)*, not user alone, because
two RomM instances both have a `User:1`. Open question 14 already established
that RomM exposes no instance identity and that "one drive, one server" is a
sentence of documentation rather than a mechanism. The same applies here: worth
a note in the layout, not machinery.

#### BUILT 2026-09-18

**It is on disk and it holds real files.** `frontend/src/storage.{h,cpp}` owns
the layout and `cache.{h,cpp}` was rewritten around it. The shape above is what
the test VM now holds, unchanged from what was agreed.

**What the code stopped having to remember.** Eviction used to enforce two rules
the disk could not express — do not delete a kept game, do not delete a save —
and it carried a list of extensions it must not touch (`.srm`, `.state`,
`.brm`) because a game's directory held its saves beside the ROM. One wrong
entry in that list would have taken the only irreplaceable thing on the machine.
Now `roms/` and `cache/` are different directories and saves are under a person,
so **eviction walks `cache/` and deletes whole entries**. The extension list is
gone, and *"eviction only ever deletes inside `cache/`"* is checkable by listing
a directory.

**Keeping is a set of people.** `users/<id> - <name>/keeps/<romId>.json` holds
the whole library entry, as before. `cache::keepers(romId)` walks every user
directory, `unkeep` removes one person's record, and only an empty result
demotes. Promotion and demotion are `rename(2)` on the entry — file or directory
— within one location, and `storage::moveEntry` reports `EXDEV` loudly rather
than quietly copying, because a crossed filesystem there would mean the layout
has a fault in it.

**A game is one entry named `<romId> - <title>`, and it is a FILE when the game
is one file.** `cache/Sony Playstation/323 - Crash Bandicoot.chd`. An archive
that unpacks into several files cannot be that, so it becomes a directory of the
same name — `cache/Game Boy/39 - Tetris/` holds the zip RomM sent and the `.gb`
that came out of it. Both are renamed identically, so nothing above this has to know which it is.
The written design showed only the file case; the directory case is what an
extracted archive forces, and 801 of the reference library's 1644 games are
`.zip`.

#### Three things building it turned up

**1. The first build spelled a platform two different ways, and it had to be
one.** Games went under RomM's `slug` because the agreed shape wrote
`roms/psx/`, and saves went under its `fs_slug` because mirroring the server was
the argument for the per-user tree in the first place. One console, filed as
`psx` on one shelf and `Sony Playstation` on the other. **MMagTech rejected it
in a sentence — does it fit the intent of a unified, organised structure — and
it plainly did not.** It is `fs_slug` everywhere now; the revision and the
Arcade fact that settles which one are recorded above.

**2. `bios/` still mixes replaceable and irreplaceable, because libretro gives a
core exactly ONE system directory. — CLOSED 2026-09-19.** The Dreamcast save
work took the card out: it is placed in `bios/dc/` for the length of a session
and moved into the person's own save tree at the quit, so what sits in there at
rest is firmware and `dc_nvmem.bin`. The original text follows.** The 13 MB of PSP system files moved out —
they ship inside the image and belong in `/usr/share/cabinetos/system/`, which
`ensureTree` symlinks into `bios/` at startup — but a Dreamcast's saved flash is
written by Flycast into the system directory and there is nowhere else for it to
go. The agreed shape has six top-level names and
none of them is "what a core wrote into its system directory", so `bios/` is
holding it. **This is the one place the shape as written does not answer the
question**, and it is left visible rather than papered over. The likely fix is
item 1 of the handover: the Dreamcast VMU work moves `vmu_save_A1.bin` into the
per-user save tree, which takes the irreplaceable part out of `bios/` for the
one platform that has it. `dc_nvmem.bin` — the console's own settings — would
still be there.

**3. `/usr/share/cabinetos` is not the core-assets directory, it is a directory
that happens to have that name.** The image already puts three unrelated files
there — a `DEVELOPMENT-IMAGE` marker and two package inventories — so linking
its contents into the console's system directory put all three where a core goes
looking for its fonts. The assets belong in `/usr/share/cabinetos/system/`, and
**`build_files/install-frontend.sh` installs them there as of 2026-09-19**; on
the VM they sit in `bios/PPSSPP/` where the core build left them and the link
step correctly leaves them alone. **Found by running it and reading the
directory listing**, which is the only reason it did not ship — the same lesson this project keeps relearning: build a thing, then
look at what it actually did.

#### Measured on the test VM, 2026-09-18

Everything below was run rather than reasoned about, on the machine with two
real filesystems. **The transcripts predate the platform rename above**, so they
show `cache/gb/` and `cache/psx/` where a console today shows `cache/Game Boy/`
and `cache/Sony Playstation/`. They are left as they came out; nothing else
about them changed, and the tree was renamed and a game relaunched from it
afterwards.

**The test VM's own files were moved onto the layout** — 12 games, four save
states, two battery saves, a PSP save folder and the BIOS — and every one was
checked with `sha256sum` before and after: 37 save-class files in, 37 out, every
hash identical. **There is no migration in the tree**, and there should not be:
nobody has run CabinetOS outside of building it, so the only machine that ever
needed moving has been moved. A console built from here starts on this layout.

**Keeping is a set of people.** Kept Mario Kart 64 as user 1 — it went straight
into `roms/n64/200 - Mario Kart 64.v64` rather than being downloaded to the
cache and moved. A second user's keep record was added by hand and the storage
report showed `kept by user(s) 1, 2`. **User 1 released it and nothing moved**:
`released by user 1, still kept by 1 other(s)`, one entry still in `roms/`, none
in `cache/`. User 1 kept it again, user 2 went away, user 1 released — and that
last release demoted it.

**The demotion is a rename.** The entry came out of `cache/` with the **same
device and inode it went into `roms/` with, `58:82064`**, so no bytes moved and
the cost is the same whatever the game weighs. For comparison, copying that same
12.6 MB across the VM's two filesystems took **0.124 s — 101 MB/s**, which puts
a 19.8 GB PS3 title at about three minutes of copying because somebody changed
their mind. That is the whole reason `roms/` and `cache/` repeat per location
rather than living once at the root, and it is now a measurement rather than an
argument.

**Eviction takes whole games, oldest first.** On btrfs with `/var` filled to
358 MB free and two cached games of 314 MB each — one a single file, one a
directory of two files, the directory a week older. Pressing Play on the 464 MB
Crash Bandicoot evicted **the directory, whole**, and stopped:

```
[cache] evicted .../cache/gb/9002 - Fake Set (314572800 bytes)
[cache] freed 314572800 bytes to make room for 487486623
```

The newer single-file game was left alone, because 358 + 314 MB was already
enough. **The old code could not have done this** — it deleted individual ROM
files and skipped anything that looked like a save, because a game's directory
held both.

**Keeping is refused before a byte moves.** The same disk, the same game, with
Download rather than Play: `refused Crash Bandicoot: 165089713 reclaimable
against a 6529167155 floor`. Both floors, checked against what the machine could
still reclaim, and no download attempted.

**A single-payload download really does become a single file.** Pokémon Red was
not on the disk; pressing Play fetched it and left
`cache/gb/2813 - Pokémon Red Version.gb`, a regular file of 1,048,576 bytes.
Tetris, whose RomM payload is a `.zip` that Gambatte cannot read as it stands,
is `cache/gb/39 - Tetris/` holding both the zip and the `.gb` that came out of
it. Its save came back from RomM into
`users/1 - MMagTech/saves/Game Boy/2813/gambatte/` and the core reported
`battery is 32768 bytes`.

**And the whole path, on a platform that needs firmware.** Twisted Metal:

```
[firmware] scph1001.bin already here
[core] loaded ./cache/psx/357 - Twisted Metal.chd
[save] restored Twisted Metal (Cabinet).srm (131072 bytes)
[save] battery is 131072 bytes
[launch] running PCSX-ReARMed
```

A BIOS found in `bios/`, a game found in `cache/psx/`, and a memory card
restored from RomM into this person's save directory — which is also the
directory the core was handed.

#### What is still not built

- **More than one storage location.** `storage::locations()` returns the root
  alone. `roms/` and `cache/` already repeat per location and every path takes a
  location, so adding the second drive is a list getting longer — but the UI is
  open question 14 and is deferred, so **"a missing drive degrades rather than
  errors" has still never been exercised**, because there is no second location
  to remove.
- **Account switching.** One user, resolved from `/api/users/me` and cached to
  `config/user.json` so a console with no network still knows whose saves it
  holds. Its own session.
- **`/var/lib/cabinetos` is not yet where this runs.** The root resolves to
  `/var/lib/cabinetos` when it can be created and written and to the working
  directory otherwise, which on the VM is `~/frontend`. The image will need that
  directory to exist and be owned by the console user; today it does not exist
  at all.

---

### PSP save sync, BUILT AND PROVEN 2026-09-17

Recorded here rather than under open question 13 because it is the first
directory-save implementation and the pattern the other seven file-writing
platforms should follow.

`frontend/src/dirsave.{h,cpp}` zips and unpacks a tree with libarchive;
`catalog::directorySaveRoot` says which core has one; `syncDirSave` in main.cpp
captures, and the launch path restores. **The archive is rooted at the SAVEDATA
level** — entries begin `ULUS10002LUMINES/PARAM.SFO` — so opening one shows the
game's save folder, which is what every PSP save download on the internet looks
like.

**Measured, not argued:**

| | |
|---|---|
| Played and quit | `Lumines.zip`, **40,344 bytes**, 4 entries, on RomM, tagged `ppsspp-native` |
| Deleted locally and relaunched | unpacked, and **byte-identical to the server's copy** |
| The old Apple-format save | sniffed, recognised as not a zip, **left alone** |

**Three things running it taught, each of which would have been wrong on paper:**

1. **Restore must happen BEFORE the core loads the game**, because PPSSPP mounts
   the memory stick while the game boots. A folder that arrives later is a
   folder the game has already decided is not there.
2. **Capture must happen AFTER the unload**, because a directory save is files
   on a disk and `retro_unload_game` is where a core flushes them.
3. **Change detection needs nanoseconds and size, not whole seconds.** A save
   restored and then rewritten inside the same second compares equal. Recorded
   as a hazard closed rather than a fault observed — it was first reported here
   as a real failure and that was wrong; checking showed the game had simply not
   written anything that run.

**And the crash it exposed, which is understood and NOT closed.** Quitting while
the game was still booting killed the console inside the core's own boot thread
(`PSP_InitStart` → `CPU_Init` → `__KernelInit` → `__PPGeInit`). Quitting now
defers until the machine is up.

> **The first fix waited a fixed number of FRAMES and fixed nothing**, because a
> frame is 3 ms or 100 ms depending on what is being drawn — the same run
> crashed at the window size and survived at 1920x1080. It waits on the clock
> now, and the case that crashed twice exits cleanly after 4.1 seconds.

**Still open:** in the headless capture configuration the core sometimes never
boots at all — one run managed 2,384 frames and zero audio — and tearing it down
then aborts at process exit in a static `std::thread` destructor inside the
core. Not seen in the ordinary configuration. Same root cause as the crash: a
core that never finished starting cannot run its own shutdown.

**The fact underneath all of it, worth more than the feature:** *a core with its
own emulation thread only advances when the frontend COMPLETES a frame, not when
`retro_run` is called.* That explains the wait loop that made no progress AND
why `--state-test` cannot warm this core up — two mysteries with one cause.

### 19. Systems this console has and Cabinet does not
**Raised and DECIDED by MMagTech, 2026-09-17.**

> *"it doesnt need to be on the other builds just this the os."*

**CabinetOS may carry systems Cabinet does not.** PS3 is the first, and Switch
would be the second.

This is a larger decision than it looks, because until now every system on this
console also existed on the Apple TV, and that was not a coincidence — it is the
premise the sync layer rests on. A PS3 game would be the first that **only**
exists here: played on the console, never appearing on the phone or the
television, with no save state travelling anywhere.

**What it changes:**

- **CabinetOS stops being "the same product on another screen" and becomes a
  superset.** That is a fair thing for it to be — the console has hardware an
  Apple TV never will — but it should be said out loud rather than discovered.
- **Core parity stops being universal and becomes conditional.** It still binds
  absolutely for every system Cabinet also ships, because that is what makes a
  save state portable. For an OS-only system there is nothing to be parity
  *with*, so the constraint simply does not apply — which also means those
  systems are free to use whatever emulator and renderer suits this hardware.
- **`catalog::coverageFor` gains no new answer.** An OS-only system is
  `Playable` like any other; what changes is that `emulatorTag` returns nothing
  for it, exactly as it already does for the cores whose builds cannot be
  vouched for. The machinery is already there.

**What it does not change:** everything Cabinet DOES ship stays in lockstep. This
is permission to add, not permission to drift.

#### PS3 is the first system where what you DOWNLOAD is not what you RUN

**Raised by MMagTech, 2026-09-17, as the one thing about PS3 he could not see
how to fit:** *"in rpcs3 you have to first install the firmware into it and then
install the game. pkg have a license as well that needs inserting and then
theres disc based iso."*

**He is right, and a first look here said otherwise.** That look checked the
name of each game's top-level entry, saw thirty directories, concluded "all disc
rips", and was wrong — it never opened them. The same shape of mistake as
believing a stale comment: one level checked, the conclusion generalised.

**Counted properly, across every file of all thirty games:**

| | |
|---|---|
| Contain a `.pkg` | **24 of 30** |
| Contain a `.rap` licence | **19 of 30** |
| Plain disc folders | **6** — God of War III is 97 files and 37 GB |

A PSN title is two files. Sly Cooper is `Sly Cooper - Thieves of Time.pkg` at
19.8 GB plus `EP9000-NPEA00429_00-SLYCOOPERPSN0000.rap`, which is **exactly 16
bytes** — measured 2026-09-18; every `.rap` in the library is.

##### Why that breaks the storage model rather than merely complicating it

Everywhere else on this console — including the six PS3 disc rips — **the file
from RomM is the artefact**. Download it, hand it to the core, done. A PKG is
not that: it has to be installed into RPCS3's virtual hard drive, which produces
a second copy of roughly the same size. Sly Cooper would be 19.8 GB downloaded
plus ~19.8 GB installed, for **forty gigabytes of one game**.

> **Superseded for any title available as a stamped ISO** — see *The better
> answer: a decrypted ISO* below, which removes the install step entirely. What
> follows still governs PKG-only titles.
>
> **Measured 2026-09-18, and the 2x turned out to be TEMPORARY** — the installed
> tree is the same size as the PKG, so deleting the PKG puts the game back at
> 1x. Sly Cooper peaked at 39.7 GB and settled at 19.8 GB. See *The PKG install,
> MEASURED 2026-09-18* below; what follows in this subsection is the reasoning
> that prompted the experiment, and it still holds for the reuse test.

So the PKG has to be deleted after installing, and that has a consequence the
cache design did not anticipate:

> **`beginLaunch`'s reuse test stops working.** It is the check open question 14
> leans on so heavily that it made drive identity unnecessary — stat the file at
> the game's rom-id path, compare its size to what the server reported, reuse it
> only if both match. For PS3 the thing on disk is an installed TREE, not the
> file that was fetched, and its size does not match what RomM said. "Is this
> game here?" becomes "is this title id installed", which is a different
> question against different evidence.

**A third state exists for PS3 and for nothing else**: fetched, installed, and
the relationship between them. Whether "kept" means the PKG or the installed
tree is a real decision, not a detail — and the answer is almost certainly the
installed tree, because that is the thing that can be run.

##### The other two steps, which are smaller than they sound

- **Firmware is one install, once per machine.** `PS3UPDAT_v4.96.PUP`, 206 MB,
  and **it is already on the server** as PS3 platform firmware — which is why
  93% of all firmware in this library belongs to a system with no core, a
  measurement recorded earlier that stops being dead weight the moment PS3 is
  real. It is not a file a core reads by name; it decrypts into a `dev_flash`
  tree. That is a category the layout does not have: derived, shared,
  machine-wide, and produced by an install rather than a download.
- **The `.rap` is per-user.** RPCS3 puts it in
  `dev_hdd0/home/<user>/exdata/`, which lands neatly in the per-user shape
  agreed in open question 18 — it belongs beside that user's saves.
- **Installing 20 GB is not instant**, so "Download" for PS3 means fetch AND
  install, and the one deliberate storage act has two phases rather than one.

##### What this does not change

The saves story above is unaffected: save data is still a folder tree under
`dev_hdd0/home/<user>/savedata/<TITLEID>/`, and the PSP mechanism still covers
it.

#### The PKG install, MEASURED 2026-09-18

**Installing a PS3 game does not cost a second copy of it.** That was the whole
worry — that a 19.8 GB download would become 40 GB on disk — and it is wrong.
The install is the same bytes moved out of the container: **the finished game is
the same size as the PKG it came from, to within a rounding error**, and once
the PKG is deleted the game costs exactly what any other game costs. The 2x is
real but it is **transient**, lasting only while both exist.

Everything below was run on the test VM, which has no GPU and no Vulkan.
**Installing needs neither** — it is decrypt-and-unpack — so the answer did not
have to wait for the A9 Max. RPCS3 came from Flathub (`net.rpcs3.RPCS3`,
`0.0.42-19980-028d1e8f Alpha`); nothing was built.

##### The four numbers

| | Super Stardust HD | Sly Cooper: Thieves in Time |
|---|---|---|
| PKG from RomM | 287,265,040 B | 19,843,204,240 B |
| Installed tree | 287,260,549 B | 19,843,198,103 B |
| **Ratio** | **0.999984x** | **0.9999997x** |
| Files produced | 59 | 53 |
| Install time | 18 s | 123 s |
| Peak disk, both present | 574 MB | **39.7 GB** |

Sly's install ran at **161 MB/s** on a four-core VM with no GPU, so a 20 GB
title installs in about two minutes. The download itself took **173 s** at
115 MB/s from RomM, so fetch and install are the same order of magnitude:
**"Download" for a PS3 PKG is roughly twice the wait of a plain download**, not
ten times.

The whole-volume measurement agrees with the per-directory one. Free space on
the games drive before installing Sly was 81,971,671,040 B; after installing,
62,128,369,664 B; after deleting the PKG, 81,971,576,832 B. **94,208 bytes from
where it started** — the installed game occupies what the PKG occupied.

##### What it produces, and where

`dev_hdd0/game/<TITLEID>/` — one directory per title, `NPEA00014` and
`NPEA00429` here. It holds the artwork and metadata the PS3 menu shows
(`ICON0.PNG`, `PIC1.PNG`, `SND0.AT3`, `PARAM.SFO`), a `TROPDIR` of trophies, and
`USRDIR` with `EBOOT.BIN` and the game's data. **Nothing is written anywhere
else** — the virtual hard drive's entire contents after installing Super
Stardust HD were that one game plus four bytes of empty directories.

**The install is self-contained and nothing records where it came from.**
`games.yml`, RPCS3's index of games held outside the virtual drive, stayed
**zero bytes** through both installs. A title in `dev_hdd0/game/` is known by
being there, and its name comes from the `PARAM.SFO` inside it: with the PKG
deleted, RPCS3 still opened Sly Cooper and logged
`Localized Title: Sly Cooper: Thieves in Time™`.

##### The PKG can be deleted, and that is proven by running the game

Deleting the PKG was tested the only way that means anything — by booting the
game afterwards. Super Stardust HD's PKG was removed, the installed tree was
unchanged (59 files, `EBOOT.BIN` at the same md5), and RPCS3 decrypted and
booted it. Sly Cooper's 19.8 GB PKG was removed and it booted too.

Neither reached gameplay, and neither was expected to: the VM has no GPU, so
this ran on the **Null renderer**, and RPCS3's PPU recompiler saturated four
cores for minutes compiling every module. **What is proven is the part the PKG
mattered for** — the installed tree is complete, self-describing and
decryptable on its own.

##### Where the `.rap` has to sit — RPCS3 says so itself

```
dev_hdd0/home/<user id>/exdata/<CONTENT ID>.rap
```

`00000001` is the user, and the name is the content id RomM already stores the
file under. Without it, booting fails with **`Failed to decrypt content`**, and
the emulator names the exact file it wanted:

> `Failed to locate the game license file: .../exdata/EP9000-NPEA00429_00-SLYCOOPERPSN0000.rap.`
> `Ensure the .rap license file is placed in the dev_hdd0/home/00000001/exdata folder with a lowercase file extension.`

Run as a control on both games with the directory empty, then again with the
file copied in, and both then booted. **The extension must be lowercase** — the
emulator says so, and RomM's filenames already are.

A `.rap` is **16 bytes**. It is per-user, which lands exactly where open
question 18 put it: beside that user's saves, not beside the game.

##### Firmware, which is one install per machine and was also measured

`PS3UPDAT_v4.96.PUP`, 206,177,436 B on the server, decrypts in **17 s** into
`dev_flash/` at **195,070,119 B** — seven top-level directories (`sys`, `vsh`,
`data`, `bdplayer`, `ps1emu`, `ps2emu`, `pspemu`). This document previously
recorded the `dev_flash` tree as a belief; it is now a measurement.

**The version does not match the filename.** `dev_flash/vsh/etc/version.txt`
reports `release:04.9200`, and RPCS3 then logs `Firmware version: 4.92`, while
the file on the server is named `v4.96`. Not investigated. Recorded because
anything that displays a firmware version should read the installed tree rather
than the filename.

##### Four things this turned up that the design has to answer

1. **RPCS3 will not install without a GUI, and refuses in so many words.**
   `--no-gui --installpkg` prints `Cannot perform installation in no-gui mode!`
   and then relaunches itself with a window, which opens a file chooser because
   the argument does not survive. **`--headless` is the one that works**: it
   builds no window at all and installs straight through. The invocation is
   `rpcs3 --headless --installpkg <path>`. This is worth knowing before anyone
   designs the install as "shell out to RPCS3 and wait" — two of the three
   obvious spellings of that command open a dialog on a console with no pointer.

2. **The exit status lies.** Both installs and the firmware install ended
   `exit=134` — SIGABRT, in a static destructor at process teardown — *after*
   logging `Successfully installed ... (title_id=NPEA00429, title=Sly Cooper:
   Thieves in Time™, version=01.00)`. The same shape as PPSSPP's teardown abort.
   **Success has to be read out of the log line, not the exit code.**

3. **An interrupted install leaves the partial tree behind.** One install was
   killed three seconds in and left a 1.5 GB `NPEA00429/` that nothing cleans
   up. The next run would have found a directory that looks installed and is
   not. **A console needs its own completion marker**, because the thing on disk
   cannot be checked against RomM's size the way a downloaded file can — which
   is the same reason `beginLaunch`'s reuse test does not work here.

4. **RPCS3 writes a recompiler cache outside the virtual drive.** It went to
   `~/.var/app/net.rpcs3.RPCS3/cache/` — **21 MB** after a few minutes of not
   even reaching a title screen, on the OS volume rather than the games one.
   That is the same class of disk as item 7's Mesa shader cache, and it will be
   much larger than 21 MB for a game that actually runs.

#### The better answer: a decrypted ISO, MEASURED 2026-09-18

**The PKG install works, and it is not the shape this console wants.** Later the
same day MMagTech pointed at a folder of games he had already converted to
decrypted ISOs — *"im thinking it would be easier to support them converted this
way then the folder structure"* — and he is right. **One file, no install, no
licence, no second copy.** Every PS3-shaped problem in the section above
disappears, and PS3 stops being a special case in the storage model: it becomes
a game file like any other.

##### RPCS3 opens a PS3 ISO itself, and the requirement is 20 bytes

RPCS3 has a disc-image loader (`rpcs3/Loader/ISO.cpp`) that mounts an ISO as
`/dev_bdvd` with no help from the frontend — no loop mount, no root, no unpack.
It accepts encrypted and decrypted images alike.

What it requires is the **PS3 disc header**, in the first two sectors:

| Offset | Bytes | What |
|---|---|---|
| `0x000` | u32 big-endian | region count, which must be 1–127 |
| `0x00C` | u32 big-endian | the last sector of region 0 — for one region, `size / 2048 - 1` |
| `0xF70` | 16 | `Dncrypted 3K BLD`, the watermark that means **decrypted**; RPCS3 then returns the data untouched |

`region_count` outside 1–127 is rejected as *"non-PS3ISO"*, which is exactly
what a plain `xorriso`/`mkisofs`/`hdiutil` image produces — its first sectors
are zero. **This header lives in the ISO9660 system area, the first 32 KB, which
the filesystem leaves empty** (ISO.h says so in as many words), so it can be
written into a finished image without rebuilding anything and without touching
the volume, the files or the size.

**Measured on the real thing, both directions.** A 12.5 GB `Bioshock.iso` built
by MMagTech's own script was rejected:

> `ISO: init: Failed to read region information (region_count=0)`
> `ISO: iso_archive: Corrupt ISO file: Decryption failed`

Stamped with the three values above, the same file was accepted and the game
booted:

> `ISO: init: Set 'enc type': DEC_3K3Y, 'reg count': 1`
> `SYS: Localized Title: BioShock`
> `SYS: Elf path: /dev_bdvd/PS3_GAME/USRDIR/EBOOT.BIN`

Zero licence errors, zero decrypt errors, and the PKG route's `.rap` is not
needed at all — a disc carries no per-user licence. Reverting the header and
letting the shipped stamper write it produced the same boot, so the tool is
proven and not just the byte layout.

**The loop-mount route also works and is the wrong answer.** Before the header
was understood, the ISO was mounted with `mount -o loop` and RPCS3 booted from
the mountpoint happily. It is recorded because it proves the image itself was
always sound — but it needs a privileged mount at launch, which is machinery
this console should not grow when a 20-byte header removes the need.

##### The fix belongs on the server, not in this console

**MMagTech's call, and it is the right one:** *"can the games on romm be fixed so
the fix doesn't live in cabinetos."* The header is part of the file. Stamped
once on the server, the ISO is correct for everything that ever reads it —
CabinetOS, RPCS3 on a PC, anything else — and **this console needs no PS3 code
at all beyond launching a file**.

Two tools were written and handed over on 2026-09-18; neither lives in this
repository, on purpose:

- **`stamp-ps3-iso.command`** — stamps images already built. It refuses a file
  whose system area is not empty rather than clobbering it, refuses anything
  that is not ISO9660, verifies by reading back, and is safe to run twice.
  `--check` reports without writing.
- **`Build PS3 ISO.command`** — MMagTech's builder, with the header written
  after each build. **Its verification was also wrong** and is fixed: it checked
  only for the ISO9660 signature, which every ISO has, so it passed every image
  RPCS3 could not read. It now checks the header that actually decides it.

**Two games are converted and verified on the server as of 2026-09-18** —
Bioshock (12.51 GB) and Bioshock 2 (11.40 GB) — both stamped, both with the
last-sector field matching the real file size. The remaining PS3 titles are
still disc folders or PKGs.

##### What this means for the storage model

**Nothing has to change.** An ISO is one file whose size RomM knows, so
`beginLaunch`'s reuse test works again — the objection raised against PKGs in
this open question does not apply. There is no install phase, no transient 2x,
no partial-install marker, and no per-user licence file to place. The four
mechanical findings from the PKG route stay recorded because PKG-only titles
still exist, but **for any title available as a stamped ISO, PS3 is an ordinary
game.**

**It is the same size as the folder it came from** — Bioshock is 12.51 GB either
way — so this buys simplicity, not space.

##### But it can only ever cover a fifth of this library

**Counted on the server 2026-09-18, and this bounds the whole idea: 24 of the 30
PS3 titles are PKGs.** A PKG is a PSN download — there is no disc behind it, so
there is nothing to make an ISO from, and no conversion script changes that.
Only the six disc-based titles can take this route:

| Shape | Count | |
|---|---|---|
| PKG — PSN downloads | **24** | install route only |
| Disc, converted to ISO | 2 | Bioshock, Bioshock 2 |
| Disc, still a folder | 4 | God of War III, Mass Effect 2, Metal Gear Solid 4, Uncharted 2 |

**So the PKG install is the majority case, not the fallback**, and the four
mechanical findings above are all still work that has to be done. The ISO route
is worth taking because it is free — the images already exist and the fix is on
the server — not because it removes the need for the other one.

##### DECIDED 2026-09-18: two shapes are supported, and the disc FOLDER is not one

**MMagTech's call:** *"both ways are supported it's just that if they aren't pkg
we only support decrypted iso and not the folder based structure."*

So this console reads exactly two things for PS3:

| Shape | How it is handled |
|---|---|
| **`.pkg` + `.rap`** | fetched and installed, as measured above |
| **Stamped decrypted `.iso`** | fetched and launched, like any other game file |
| ~~`PS3_GAME/` folder~~ | **not supported.** A source format, to be converted first. |

**This removes work rather than adding it, which is why it is the right call.**
A disc folder is not one file and not a few — counted on the server: Uncharted 2
is 364 files, Metal Gear Solid 4 is 328, and **Mass Effect 2 is 8,337**.
Downloading a tree that size from RomM one file at a time is a transfer path
this console does not have, has never tested, and would have to grow a progress
model, a resume story and a partial-tree check for. Converting to an ISO turns
all of it into a single download that every existing mechanism already handles.

**Nothing is built for disc folders — not even a way to say they are not
ready.** MMagTech, immediately after the decision: *"we are not building for
those folders or to support them they are currently being converted to iso."*
The four remaining ones — God of War III, Mass Effect 2, Metal Gear Solid 4 and
Uncharted 2 — are mid-conversion, so the state is temporary and does not need
code to describe it. **This console's PS3 support is a PKG or a stamped ISO, and
the folder simply never reaches it.**

**Still not playable here.** Every ISO result above is a boot to the point of
loading the executable; the VM has no GPU, so nothing has been played. That
waits on the A9 Max and open question 20.

#### PS3's saves, and why the missing snapshots do not matter

**Recorded because this document briefly implied otherwise and MMagTech
corrected it twice.**

- **PS3 save DATA exists and is a folder tree**, on RPCS3's virtual hard drive —
  roughly `dev_hdd0/home/<user>/savedata/<TITLEID>/`, a directory per game.
  **That is the same shape as PSP's memory stick**, which means the mechanism
  built on 2026-09-17 for PSP — zip the tree, upload it, unpack it before the
  core boots, compare against a baseline to know what moved — is what PS3 uses
  too. `dirsave.h` was the first customer for a pattern, not a one-off.
- **What RPCS3 lacks is SAVE STATES**, the mid-game snapshot, which is a
  different feature from the console's own saves. *(Believed rather than
  verified; check it before relying on it.)*
- **And that does not matter.** MMagTech: *"snapshots arent needed on systems
  like the ps3 since they have memory cards or in game saves."* Correct, and it
  is the right frame — a save state earns its keep on a cartridge-era machine
  that gives the player nothing, or one checkpoint an hour. A PS3 game saves
  properly by itself.
- **The UI already knows how to say so.** Cabinet hides the save-state slots
  outright for the two cores that cannot serialize, rather than offering a
  button that fails. "This platform does not do snapshots" is an honest state
  the design already expresses.
- **It is doubly moot here**, because PS3 is OS-only: there is no Apple build
  for a state to travel to. The absence costs the in-game snapshot on this
  console and nothing else. Progress still moves, because progress is save data.

### 20. Vulkan, and how the host should choose a graphics API
**Raised by MMagTech 2026-09-17. BUILT 2026-09-20, and it is what made
PlayStation 2 and GameCube draw a picture.**

#### What it bought, first, because that is the point

**85 games.** PS2 71 and GameCube 14 — the largest missing tier in the library.
Both cores already existed, both already had Vulkan compiled in, and
`catalog` has routed `ps2 → pcsx2` and `ngc → dolphin` since the table was
written. The only thing missing was the FRONTEND's half of a contract libretro
already specifies. RetroArch implements that end; this console owns its
frontend and had implemented the OpenGL ES half only.

#### The two failures, measured before anything was written, had one cause

- **PCSX2 refuses a GLES context outright** — `OpenGL is not supported. Only
  OpenGL 3.2 was found` — and then runs the emulated machine perfectly, makes
  48 kHz audio and draws nothing.
- **Dolphin renders from a thread of its own.** Mario Kart booted, ran fifty
  seconds of emulated time with correct audio, and left the entire 1920x1080
  frame at the letterbox glow. Ikaruga did not get that far: it died in
  Dolphin's PowerPC JIT, on a guest memory read through a 64 GiB fastmem arena,
  with no signal handler in the process to catch it.

**A GL CONTEXT BELONGS TO ONE THREAD AND VULKAN HAS NO CONTEXT AT ALL.** That
is the whole difference and it is why this is the right answer rather than the
cheap one: a device and a queue are objects any thread may use. Ikaruga's crash
went away as a side effect of moving off GL.

**The control proved it was not the picture path.** `--core-no-hw-render`
refuses a core hardware rendering however serveable the ask was. Dolphin
crashed identically with no picture at all, which is what separated "cannot
get a picture out of this host" from "cannot run in this host".

#### Three one-line faults, and the third is the one nobody would guess

1. **`GET_PREFERRED_HW_RENDER` was hard-wired to `OPENGLES3`.** So Dolphin
   asked what the frontend preferred, was told OpenGL ES, and dutifully took
   it — never asking for the Vulkan it also has. **This one line decides which
   API every multi-API core uses.** It now answers whichever the machine has.
2. **`SET_HW_RENDER` refused Vulkan by name**, with a comment saying so.
3. **Dolphin decides whether it has a display by whether the frontend handed
   it a `VkSurfaceKHR`.** With none it renders and never presents. It gets a
   **`VK_EXT_headless_surface`** — a real surface with no window — and every
   surface and swapchain call is intercepted by the core anyway
   (`DolphinLibretro/Vulkan.cpp` replaces `vkCreateSwapchainKHR`,
   `vkAcquireNextImageKHR` and `vkQueuePresentKHR` wholesale). The surface is
   a token meaning "there is a display".

#### The UI stays on OpenGL ES, and that is a decision

Every screen was drawn and judged in GLES, and **the test VM has no Vulkan
device** — `--gpu-probe` reports *"the only Vulkan device here is a software
one"*. A Vulkan-only frontend could not run on the machine this project is
developed on. The picture crosses with no copy: Vulkan exports the image as a
dmabuf and EGL imports the same memory as a texture, which the A9's radeonsi
supports in both directions.

#### What it costs, and TWO OPTIMISATIONS THAT ARE BOTH WRONG

| | per picture | of a 60 Hz frame | emulated speed |
|---|---|---|---|
| PS2, native 640x448 | 0.262 ms | 1.6% | **10.1x realtime** |
| PS2, 4x upscale | 1.309 ms | 7.9% | **6.3x realtime** |
| GameCube, Mario Kart | 0.226 ms | 1.4% | **4.6x realtime** |

**An optimally-tiled destination negotiated through
`VK_EXT_image_drm_format_modifier` was BUILT AND MEASURED, and removed.** Four
modifiers agreed between Vulkan and EGL, driver chose `0x200000010401b04`:

| | linear | optimal |
|---|---|---|
| PS2 native | 0.269 ms | 0.247 ms |
| PS2 4x | 1.323 ms | **1.349 ms** |
| wall clock, 1800 frames | 5.71 s | **5.89 s** |

Nothing, and worse at the resolution it was meant to help. **Splitting the cost
says why, and kills the exported-semaphore idea in the same breath:**

```
1x native:   0.011 ms recording the copy,  0.250 ms waiting for the GPU
4x upscale:  0.012 ms recording the copy,  1.297 ms waiting for the GPU
```

**The copy costs twelve microseconds.** The rest is the frontend discovering
that the EMULATOR has not finished drawing, because the core's rendering is
queued ahead of our copy on the same queue. A semaphore would not make the GPU
finish sooner — it would only free a CPU whose next job is to draw the UI with
the picture it is waiting for.

**MMagTech, 2026-09-20: upscaling will be an option, and not everyone runs a
powerful mini PC.** That was a good reason to go and look, and looking produced
a better answer than the recommendation it was testing: **upscaling costs what
it costs because the emulator draws more pixels, and none of that is interop
overhead.** On a weaker machine the answer is to turn the upscale down.

**Do not rebuild either optimisation without a measurement that contradicts
the tables above.** The numbers live in `vkhost.cpp`'s `createShared` as well
as here, because that is where somebody will be standing when they have the
idea.

#### The old recommendation, which held up



Three cores render with hardware — Flycast, Mupen64Plus and PPSSPP. Everything
else hands back a finished picture and none of this touches it.

**All three already have Vulkan compiled in, and all three run on GLES.**
Measured on the built artifacts rather than assumed:

| Core | Vulkan symbols in the `.so` | What it asks for |
|---|---|---|
| Flycast | 353 — built `-DUSE_VULKAN=ON` | OpenGL ES 3.0 |
| Mupen64Plus | 570 — this is **parallel-RDP**, a Vulkan renderer, built in deliberately | OpenGL ES 3.0 |
| PPSSPP | 97, and it declares `ppsspp_backend = auto` | OpenGL ES 2.0 |

They run on GLES because **the host owns a GLES context and refuses anything
else by name**, so that is what `GET_PREFERRED_HW_RENDER` advertises and what
the cores take. PPSSPP's option says `auto`, and auto means "whatever the
frontend prefers" — it is not a hardware probe.

#### The recommendation: discover, then fall back

**Not for NVIDIA's sake, though it serves it.** MMagTech raised future NVIDIA
support as the reason to keep things automatic. The stronger reason is today:
**the test VM has no Vulkan at all** — this document already records gamescope
rejecting llvmpipe because Vulkan enumeration finds no devices. A Vulkan-only
host would not run on the machine this project is developed on.

So the shape is the one *Hardware* already states as a rule — capability is
discovered, not assumed:

1. The host probes at startup: Vulkan where the machine has it, GLES where it
   does not, and it advertises whichever it got.
2. Cores stay on `auto` and follow.
3. A core asking for something the host cannot serve is still refused **by
   name**, in one line, rather than being allowed to fail inside the core where
   it reads as a broken game.

NVIDIA then costs nothing extra here. Its real cost is unchanged and lives in
open question 11: a second base image to build, sign and boot.

#### What renderers do and do not affect

**Save states are not affected, and a claim here that they were is withdrawn.**
MMagTech, correctly: *"save states have nothing to do with rendering like gles
or vulkan just the core version."* A state is the emulated machine — the host's
graphics API is not in it. The parity rule is about the core and its build, and
nothing about GLES or Vulkan touches it. An earlier version of this section
suggested pinning PPSSPP to GLES to protect its shared tag; that was wrong and
the pin is not needed.

**A graphics PLUGIN is a different thing from a graphics API, and N64 is the
case that proves it.** Moving Mupen64Plus to parallel-RDP is not merely
presenting through Vulkan — it swaps the emulated RDP implementation. This
document already lists *"graphics-plugin state that lives outside the state"* as
one of three unresolved suspects for N64's save states not restoring exactly. So
N64 deserves care, for reasons that have nothing to do with Vulkan.

#### Why this is worth building

One piece of work serves three things at once: **PS3 needs it** (RPCS3's good
renderer is Vulkan), **N64 wants it** (parallel-RDP is already compiled in and
cannot be reached), and **Dreamcast benefits** (Flycast's Vulkan renderer is
generally the faster one). That is a better reason to teach the host a second
API than "PS3 needs it".

**Untestable until there is hardware.** The VM has no Vulkan, so none of this
can be measured before the A9 Max is installed.

### 21. Emulators that are not cores, and why they cannot be baked into the image
**Raised by MMagTech 2026-09-18. DECIDED and BUILT the same day.**

Three emulators this console wants are not libretro cores and never will be:
**RPCS3, xemu and Eden** are standalone applications with their own renderers.
They come from Flathub rather than being built in `cores/`.

The question was whether to install them at image build time. **No, and the
reason is not a preference.**

#### The trap, which builds green

A flatpak installs into `/var`. bootc treats `/var` in a container image like a
Docker `VOLUME`: its contents are unpacked **only from the initial image**, and
the upstream documentation is explicit that *"subsequent changes to `/var` in a
container image are not automatically applied"*.

So `flatpak install` in the Containerfile gives:

| | |
|---|---|
| Brand new machine | the emulator lands, once |
| Every image after that | **nothing changes, ever** — a newer emulator never arrives |
| Every machine that already exists | **nothing at all**, including the test VM |
| The build | **green, throughout** |

**Half of what this project was told about it is wrong, and checking mattered.**
The claim was that flatpak "can't install at build time because `/var` isn't
part of the image, so it installs nothing". Built in a container on 2026-09-18:
`flatpak remote-add --system` succeeds and writes `/var/lib/flatpak`. Flatpak
runs perfectly well at build time. **The mechanism works and the semantics do
not**, which is a worse failure than the one described, because it produces a
machine that looks correct and an emulator frozen at whatever shipped the day it
was installed.

It also breaks the requirement outright: baked in, an emulator's version moves
**never**, not with the image.

#### What is built instead

The image carries the **list and the pinned revisions**; the machine does the
install. `system_files/usr/share/cabinetos/flatpaks.list`,
`/usr/libexec/cabinetos-flatpak-setup`, a oneshot service and a retry timer,
wired in by `build_files/configure-flatpaks.sh`.

**This is the same model as the cores**, which is the point.
`cores/build-core.sh` pins each core to an exact `COMMIT=`; this pins each
flatpak to an exact commit. In both cases the revision lives in the image and
moves only when someone edits it and ships a new image.

Two things make the pin hold rather than merely intend to:

1. **`flatpak-system-update.timer` and `uupd.timer` are masked** by
   `strip-desktop.sh` — originally to stop partial-version states, and now
   load-bearing for this. `configure-flatpaks.sh` **fails the build** if either
   is ever unmasked, because pinned emulators would silently start drifting.
2. **The install step always names the commit.** `flatpak install` takes
   whatever the remote serves today; `flatpak update --commit=` moves it to the
   exact revision.

**Verified on the VM 2026-09-18, in both directions**, because "pinning works"
is the kind of claim this project has been wrong about before:

| | Version |
|---|---|
| Pin forward to a newer commit | `0.0.42-20022-8db660b1` |
| Pin back to an older one | `0.0.42-19980-028d1e8f` |
| Plain `flatpak update`, no commit | `0.0.42-20022` — **drifted** |

That last row is not hypothetical. **Flathub moved RPCS3 on the morning of
2026-09-18**, while this work was happening: the build every PS3 measurement in
open question 19 was taken on is already not what a new install would fetch.

#### The runtime is pinned too

**MMagTech's call, and the reason is support rather than tidiness.** A flatpak
sits on a shared runtime carrying its Qt, graphics and audio libraries — KDE
6.11 for RPCS3 and Eden, Freedesktop 25.08 for xemu. Pinning only the
application would mean two machines installing the **same CabinetOS image** a
month apart get different libraries under the same emulator: same version
string, different behaviour, and a bug report whose answer depends on the date
somebody installed. Pinning both means **the image version alone says what is on
the machine**.

The honest limit: runtime *extensions* — `GL.default`, `Locale`, the codec
bundles — follow their runtime rather than being pinned individually. Pinning
every extension is not practical and has not been attempted.

#### First boot must not be able to fail

**An unreachable Flathub must not break setup.** A console whose first boot
fails because a CDN is down is worse than one whose PS3 support arrives late, so
the installer logs every failure, **always exits 0**, and writes its
"done" stamp only on a clean run. The timer comes back every six hours, so a
machine first booted without a network converges on its own.

This was measured rather than asserted — the script was run against an
unreachable remote, a malformed manifest and a missing manifest, and exits 0
with no stamp in every case.

#### Nothing tells the person it is downloading — NOT SETTLED, 2026-09-18

**MMagTech's first lean was silence:** *"right now im leaning toward it just
being silent and including that requirement in documentation."* **He then
withdrew it, and was right to:** *"its not a settled decision because the
network requirement doesn't necessarily mean the OS has external access to RomM
as someone could be running this all on just their internal LAN."*

**The argument written here for silence was wrong, and this is the correction.**
It said a network is not an extra requirement because every game comes from RomM
anyway. That conflates two different networks:

| | Needs | A LAN-only machine |
|---|---|---|
| RomM, and therefore the entire library | **the local network** | **works completely** |
| Flathub, and therefore PS3, Xbox, Switch | **the internet** | **never works, ever** |

RomM is a server on the person's own LAN — this project's is at
`192.168.1.10`. A console on an isolated network has a full library, 1147
playable games and twenty-one working emulators, and **three systems that will
never arrive no matter how long the timer runs.** That is not a delayed
condition that resolves itself; it is permanent, and under the silent design
nothing would ever say so.

**That is a materially worse case than the one silence was chosen for.** A
temporary outage is a machine that fixes itself within six hours. A deliberately
offline install is a machine that retries every six hours forever and never
succeeds, with no indication anywhere.

**So this is open, and it is not only a UI question.** Two parts, and the first
is a mechanism rather than a picture:

1. **Does CabinetOS support an internet-free deployment at all?** If it does,
   Flathub cannot be the only route to these three emulators, and something else
   is needed — carrying the flatpaks in the image is exactly what `/var`
   forbids, so the candidates are a sideload path, an install from removable
   media, or mirroring them onto the RomM host. None is designed.
2. **If it does not, that has to be stated as a requirement**, not discovered by
   someone whose Switch games never appear.

The UI half still waits for a television: a missing system and a
*still-arriving* one look identical, and `catalog::coverageFor` has four answers
and none of them is "present, but still downloading". **But a permanently absent
system is a fifth case again**, and telling those two apart is the part that
cannot be silent.

#### What it costs, and the thing to watch

**Roughly 2.5–3 GB into `/var` on first boot** — KDE 6.11 alone is 1.1 GB
installed, and xemu pulls a second runtime. That is real and it lands on the
same volume as the disk floors, on a machine whose `/var` the test VM shows at
under 5 GB free. **It has not been sized against the floors**, and it should be
before anyone installs on the A9 Max.

Two more open edges:

- **Nothing launches these yet.** The frontend hosts libretro cores in its own
  process; RPCS3 is a separate application with its own window, which is
  **open question 12** and unanswered. This question is about getting the
  emulator onto the machine at a known version, and nothing more.
- **xemu and Eden serve zero games today** — the systems table above counts 0
  Xbox and 109 Switch titles, and Switch is the one with games. They are in the
  manifest to prove the mechanism generalises, which was the requirement.

### 23. One quality setting for the whole console, instead of emulator menus
**Raised by MMagTech 2026-09-20. Surveyed the same day against all 23 cores.
Not designed, not built.**

> I hate messing with settings in emulators. What I'd want me or anyone to
> experience is something like a performance and quality setting that affects
> all cores.

**This is the difference between a console and a frontend, and it is the right
instinct.** RetroArch's answer to "should this look better" is a menu per core;
a console's answer is that somebody already decided. Everything below is about
making that decision once, per platform, rather than asking.

#### THE SURVEY, because the problem is much smaller than it looks

Every option every core declares, read off the running console 2026-09-20:

| | |
|---|---|
| **A real resolution lever** | **7 systems, 6 cores** — PS2, GameCube, PSP, N64, Dreamcast/Naomi, PlayStation |
| Only a LOOK filter | Mega Drive's NTSC filter, Game Boy's LCD filter, Master System, 7800, 2600 |
| Nothing to choose | Saturn, Neo Geo Pocket, Virtual Boy, DS, NES, arcade, Vectrex, Virtual Boy |

So it is not "a setting that affects all cores". It is **a setting that affects
the seven systems where it means anything**, and that is a line a person
already understands without being taught it.

**AND IT IS NOT "THE HARDWARE-RENDERED CORES", which was the obvious wrong
answer.** `pcsx_rearmed` renders PlayStation in SOFTWARE and still has
`neon_enhancement_enable`, `scale_hires`, `dithering` and
`gpu_thread_rendering` — four real levers. Meanwhile Dolphin declares *nothing*
until a game is loaded, so a survey of what a core reports at load time misses
it entirely. The right test is whether the SYSTEM has an internal resolution
worth raising, not how the core draws.

**The 2D cores' options are a different kind of thing and must not be dragged
in.** `snes9x_overclock_cycles` and `genesis_plus_gx_overclock` trade accuracy
for compatibility, not quality for speed, and on any machine this OS runs on a
SNES is not a performance problem. Those get set correctly once and are never
part of a performance choice. `blargg_ntsc_filter` is a LOOK, free, and belongs
wherever the shader and glow settings end up — not here.

#### The hardware is unknown and that is not the problem it looks like

The worry is real — this runs on whatever AMD machine somebody installs it on,
from a small APU to a large card, and open question 11 adds NVIDIA later. A
hardware database would be wrong about the first chip nobody tested, and a
synthetic benchmark at first run does not predict emulator speed.

**The console already knows how fast it is going.** `core.h`: *"audio against
the core's own sample rate is the only direct read on whether emulated time is
advancing at realtime"*. That is the emulated machine reporting its own speed
on the actual game, which is the only measurement that matters and it needs no
table of GPUs.

Measured on the A9 Max, 2026-09-20: **PS2 10.1x realtime at native and 6.3x at
a 4x upscale; GameCube 4.6x.** A machine with a quarter of that headroom shows
it in the same number.

#### The recommendation

1. **Three levels in console language — Performance, Balanced, Quality.** Never
   an emulator's vocabulary. "Upscale multiplier" is not a thing a person
   should have to have an opinion about.
2. **A per-platform table mapping each level to real option values, with a
   reason recorded per line.** This is open question 7's existing ask given a
   better shape: `catalog::optionOverrides` takes a core today, and taking a
   core AND a level is a change of shape rather than a new subsystem.
3. **Apply at launch, never mid-game.** Dropping resolution in the middle of a
   race is worse than a slightly low frame rate — it is visible and it reads as
   broken. Measure during play; act at the next launch, or say *"this ran at
   72%, try Performance?"* and let the person decide. A console that changes
   itself underneath somebody is not calm, it is haunted.
4. **A per-game override**, because one game is always the exception. Cabinet
   has the precedent and the reason: it keeps renderer and aspect per game for
   PS2 because *"which renderer a title needs is a fact about the title"*.

#### THE HONEST OBSTACLE, AND IT IS NOT THE CODE

**There is only one machine to tune against, and it has 6 to 10x more headroom
than it needs.** A Performance level cannot be tuned on hardware that never
needs it — anything written for it would be a guess wearing a number. So the
first version of this is **getting the DEFAULT right per platform**, which the
A9 can answer, and the lower tiers wait for either a second machine or a report
from somebody running one.

That also sets the acceptance test, and it is not a screenshot: **a person
plays each of the seven systems and nobody reaches for a menu.**

#### THE AUDIT IS DONE — `docs/CORE-OPTIONS-AUDIT.md`

All 23 cores, 825 options, read out of the loaded `.so` rather than out of
upstream documentation. **Eight systems have a resolution worth raising and
for each it is ONE option**; the other fifteen have nothing to move.

| | |
|---|---|
| PlayStation 2 | `pcsx2_upscale_multiplier` — 1x / 2x / 4x / 8x |
| GameCube | `dolphin_efb_scale` — 1 to 6 |
| PSP | `ppsspp_internal_resolution` — 480x272 to 3840x2176 |
| Dreamcast, Naomi | `reicast_internal_resolution` — 320x240 to 12800x9600 |
| Nintendo 64 | `mupen64plus-43screensize` OR `-parallel-rdp-upscaling` |
| PlayStation | `pcsx_rearmed_neon_enhancement_enable` |
| 3DO | `opera_high_resolution` |
| Arcade (FBNeo) | `fbneo-resolution` — 640x480 to 2880x2160 |

**Three things the audit corrected that would have been guessed wrong:**

- **It is not "the hardware-rendered cores".** PlayStation renders in SOFTWARE
  and has a real lever; **3DO and FinalBurn Neo have one each** and were on
  nobody's list.
- **Three cores declare nothing until a game is loaded**, and they are three of
  the most configurable: Dolphin reports **zero** options at load and **103**
  with a disc in, and FBNeo and MAME are per-driver. An audit taken at core-load
  time reports zero for exactly the cores the question is about.
- **Nintendo 64 has TWO RENDERERS** — gliden64 and paraLLEl-RDP — which are
  different emulations of the same chip with separate scaling options. "The N64
  resolution setting" is two settings behind a choice of plugin, and this
  document already lists graphics-plugin state as a suspect for N64's save
  states not restoring exactly. Not one to move casually.

#### AND A WARNING ABOUT MEASURING THE COST

PlayStation 2, Homura, 1800 frames each, same machine, same session:

| | realtime |
|---|---|
| 1x native | 6.02x |
| 2x native | **2.33x** |
| 4x native | **5.09x** |

**Those numbers are wrong and they are kept here deliberately.** 2x cannot be
slower than 4x. It is shader compilation on the first run at a new resolution,
which means **a single run is not a measurement** — anyone building a tuning
table from one will be tuning against their own shader cache. Warm runs, and
repeats.

#### What makes it cheaper than it sounds

The instruments went in with the PS2 and GameCube work. `--core-option
key=value` tries any candidate value on a real game with no rebuild, and
`--core-options` dumps what every core actually offers. The measuring is
already possible; the expensive part is the DECIDING, one platform at a time,
with a game in front of you.

### 22. What the console does when the server is away
**Raised by the A9 Max's first reboot, 2026-09-19. Partly decided the same day.
Not built.**

**REPRODUCED IN FULL ON 2026-09-19**, on the first boot after the A9 was moved
onto the image, and worth reading because the whole sequence is in one journal
and every step of it is quiet:

```
[romm] nothing answered at 192.168.1.10:6005 over http or https
[gamescope] launch: Primary child shut down!
cabinetos-session: gamescope (drm) died on startup
cabinetos-session: WARNING — no Vulkan-capable GPU. Falling back to cage.
```

**The message is a lie, and it is the machine's own log that proves it.** Four
seconds earlier the same gamescope had printed `vulkan: selecting physical
device 'AMD Radeon 890M Graphics (RADV STRIX1)'` and `drm: selecting mode
3840x2160@60Hz`. It had a Vulkan GPU, it had set a mode, and it exited only
because its child did. The console then ran the whole session on llvmpipe with
nothing on screen saying so, and recovered only when the service was restarted
by hand.

**This is now the thing most likely to make the reference machine lie to
whoever looks at it next**, because the failure looks exactly like a working
console. Anyone judging the look on the A9 should read
`journalctl -t cabinetos-session | grep 'is up'` first and check which rung it
landed on.

**The console currently dies.** The first cold boot after an upgrade came up
faster than the network did, `romm::Client::setAddress` failed, the frontend
returned 1, and — because gamescope exits when its primary child exits — the
session's compositor ladder concluded that *gamescope* had failed and
permanently demoted the machine to cage on llvmpipe. A transient network race
at boot cost hardware rendering for the rest of the session, silently, and
nothing on screen said anything. **Every power cut will hit this.**

That is three separate faults. **The two that make the machine lie about
itself are fixed, 2026-09-19**; the third is deliberately not done.

1. ~~**The frontend must not exit when the server is unreachable.**~~ **It now
   waits ninety seconds for it**, saying so once, and says so again when the
   server answers. That covers a boot race and a router coming back after a
   power cut, which is what the fault actually was. It is NOT the offline
   console — a machine that keeps its library and plays its kept games with no
   server is the design below, and is still owed.
2. **The session ordering after `network-online.target` was considered and NOT
   done.** It would delay the picture on a console with no network at all,
   by however long `nm-online` takes to give up, in exchange for closing a race
   the retry above already closes. A console showing nothing for a minute
   because it is waiting to be told there is no network is a worse failure than
   the one being fixed. Recorded as a decision rather than an omission.
3. ~~**The ladder must tell "the compositor failed to start" apart from "the
   app exited".**~~ **It does.** The old test was "is the process alive five
   seconds after launch", attributed to the compositor — and gamescope exits
   with its child, so a frontend quitting at one second was indistinguishable
   from a GPU that cannot do Vulkan. It now ASKS: gamescope's own `--ready-fd`
   is written the instant the compositor is serving, with a Wayland socket
   appearing as the backstop for cage, which has no such flag. Once a
   compositor has come up, a dead child is the app's exit and the session exits
   with its status for systemd to restart at the top rung, rather than falling
   down the ladder.

   **Measured on the A9 against the real failure**, by pointing the session at
   an unreachable server under the real service:

   ```
   cabinetos-session: trying gamescope (drm)
   cabinetos-session: gamescope (drm) is up
   [frontend] GL_RENDERER AMD Radeon 890M Graphics (radeonsi, strix1, ACO...)
   cabinetos-session: gamescope (drm) exited with 0 AFTER coming up — that is
                      the app's exit, not a display fault; not falling back
   ```

   It cycles on the real GPU until the server answers, instead of demoting to
   llvmpipe until somebody notices.

#### The stand-in library must never appear on a console

Today's fallback when there is no server is the built-in demo library. On a
development VM that is useful. On a television it is the worst available
answer: it looks like a working console showing somebody else's games, and
nothing says the real library is missing.

#### What to show instead, escalating with time

A console switched on at the same moment as the router must not accuse anyone
of anything two seconds later.

| When | On screen |
|---|---|
| First few seconds | the boot splash simply stays up — nothing has gone wrong yet |
| ~10 s | *"Finding your library…"*, quiet, no alarm |
| ~30 s | which thing is actually missing, and what to do about it |

**And it corrects itself with no input.** The moment the server answers it
goes to Home. Power comes back, the router takes forty seconds, the console
arrives on its own.

**Three different problems must not read as one**, or it is the
truncated-explanation trap this document already warns about: no network at
all (check the cable or Wi-Fi), network but no server (is the machine at that
address switched on), and server but not paired (that is first run, open
question 15b).

#### Kept games play offline. The library does not. — Cabinet's rule

**MMagTech, 2026-09-19: "it seems natural that no network should still allow
you to play offline if a game is kept."** Cabinet already settled this, and
its answer is narrower and cheaper than caching a catalogue. From
`OfflineNotice.swift`:

> The library itself stays server-only on purpose, no snapshot of it kept
> locally, so this is still the honest answer for browsing. **Kept games are
> the one deliberate exception: Home shows those directly instead of this
> notice when any exist, since they genuinely do play with no connection.**

So browsing 1,600 games offline is not a goal — you could not play them
anyway. What was kept, plays.

**A keep writes down everything about the game at keep time**, which is the
part this console does not do yet. `KeptGames.swift`:

> Embeds the whole `Rom` it was kept from, captured once at keep time, rather
> than a hand-picked subset of fields. Offline navigation … needs everything a
> live library fetch would have given it, **cover paths and platform
> identifiers included**, and re-deriving a partial, patched-together `Rom`
> later would only invite the fields to drift apart.

Our layout gives the id and the name back for free —
`roms/Sega Dreamcast/556 - Ikaruga.chd` — but **not the cover**, and covers
live only in the in-memory image cache. Keeping a game therefore has to save
its cover and a record beside it.

**Two more rules worth taking verbatim**, both from `offlinePlatforms()`:

- **Offline Home and offline Library are built from the same list**, "so the
  two can never draw a different picture of the same underlying data".
- **The count shown is how many are kept**, not the server's catalogue size,
  "which would mean nothing without a connection to trust it".
- And **a kept game whose emulator is unavailable is hidden**, because
  "listing them would set up a tap that fails regardless of what is actually
  stored". Here that means a game whose core this console has not built.

#### Saves offline: the disk always wins first

`MemoryCardStore`:

> The disk copy is written first on every snapshot, before any upload is
> attempted … **losing signal must never mean losing an in-game save.** A card
> whose upload has not yet succeeded carries a pending flag and is retried at
> the next launch.

And the launch-time precedence, from `MemoryCardSync.syncIn()`:

| | |
|---|---|
| 1 | a local copy **still waiting to upload** wins — it is strictly newer than anything the server has |
| 2 | otherwise the **server's** copy, when its stamp moved since the last sync — a save made on another device |
| 3 | offline, or nothing on the server — **whatever is on disk plays** |
| 4 | nothing anywhere — the core's own freshly formatted card |

Retries happen **at the next launch**, not from a background service.

**What exists here already**: `users/<id>/pending/` and a save path that
writes locally before syncing. **What is missing**: the per-save bookkeeping —
a pending flag and the server's stamp — and the precedence above.

**The accepted trade, so it is a decision rather than a discovery.** Rule 1
means an unsent local save always beats the server, so playing offline on the
console and then playing the same game on an Apple TV before the console
reconnects loses the Apple TV's save. **MMagTech's reasoning, 2026-09-19, and
it is why this is accepted: saves are per person and this console switches
users, so two people on two devices are two different rows and never
collide.** What remains needs one person, two devices, sequentially, with the
console offline in between. Rare, and the alternative is asking someone to
resolve a merge conflict on a television.

#### Who the console is with no server — DECIDED 2026-09-19

Identity comes from RomM's `/api/users/me` and is cached in
`config/user.json`, which holds **one** user: the last one seen. So an offline
console knows who it is and cannot know who else exists.

**MMagTech's call: offline, the console stays as the last user it knew.** It
says so plainly and **does not offer a switcher it cannot honour.**

That is the safe answer as well as the simple one. The failure it avoids is
worse than any save conflict: someone switching to a person the console cannot
verify, then playing, and having their saves filed under the wrong user and
the wrong person's games on the kept shelf — silently, and only noticed later.

Caching the user *list* would allow offline switching, and it is not decided
here; it belongs with account switching, which is already its own topic.

### 24. Will gamescope composite our overlay over a window we do not own?
**Raised by MMagTech, 2026-09-21, before any more measuring. ANSWERED THE SAME
DAY, on the reference console's own television. YES — and the slot it has to
live in is not the obvious one.**

#### Why it was asked

PCSX2 renders the picture on the GPU. To get it onto the television this console
copies it OFF the card, hands it over, and copies it BACK ON to draw it. A
normal PCSX2 — on Windows, on Bazzite — never does this: it draws straight to
the screen. We do it so the console can draw its own pause menu over the game.

That copy costs **6.1 ms on average and 12.2 ms at worst** at 4x, paced to
60 Hz: 37% and 73% of a frame budget. MMagTech, 2026-09-21: *"i didnt buy this
mini pc to be gimped in performace especually compared to it running on
windows."* The standard is right and the hardware is not the constraint.

**It was never a PlayStation 2 question.** The twenty-one libretro cores do not
have this problem and must not be moved: there the FRONTEND creates the device
and lends it to the core, which renders into a texture the console already
owns. It is the emulators that are *not* libretro cores — PCSX2, standalone
Dolphin, RPCS3, xemu, Eden — and MMagTech has said PS3, Switch, Xbox and Wii U
are all coming. Whatever this question answers, it answers for all of them.

#### The answer

**Yes on all three counts, verified on the A9 at 3840x2160 under gamescope/drm:**

| | |
|---|---|
| Composites a window we do not own | **Yes.** glxgears drew over a vkcube it has no relationship with. |
| The game shows through our transparency | **Yes.** MMagTech, looking at the television: *"i can see through the green"* — a 50% band with the game visible beneath it. |
| Our overlay can take the pad, and give it back | **Yes**, one atom, while the game keeps the screen. |

**All three were then confirmed TOGETHER in the `STEAM_OVERLAY` slot**, which is
the only one a pause menu can use, with MMagTech watching the set: *"yes magenta
is therre and i can see throught the green bar and the cube keeps spinning"*.

**"The cube keeps spinning" is the part to notice.** The game goes on rendering
and presenting while the overlay holds keyboard and pad focus. That is not a
screenshot of a paused game with a menu drawn on it — it is the live game
underneath a live menu, which is what this console's pause menu already is for
the libretro cores.

The input result is the one that matters, and it is exactly a pause menu:

```
Global focus window:          0x400000 (Vkcube X11)            <- game keeps presenting
Global input focus window:    0x600002 (cabinetos overlay probe)
Global keyboard focus window: 0x600002 (cabinetos overlay probe)
```

Setting `STEAM_INPUT_FOCUS` back to 0 hands input straight back to the game.
Pause and Resume are one property each.

#### THE TWO SLOTS ARE NOT THE SAME, and this is the load-bearing detail

`GAMESCOPE_EXTERNAL_OVERLAY` is the obvious one and it is **the wrong one**. It
composites, and it can never take input:

```c
if ( w->isOverlay && w->inputFocusMode )   /* STEAM_OVERLAY, not EXTERNAL */
    inputFocus = w;
```

`isOverlay` is the `STEAM_OVERLAY` atom. The external-overlay slot is the HUD
slot — it is what mangoapp uses, and a HUD never needs the pad. **A pause menu
has to live in the `STEAM_OVERLAY` slot**, with `STEAM_INPUT_FOCUS` toggled when
the menu opens and closes. That is the slot Steam's own overlay uses to draw
over a game it does not own and take the controller, which is the same problem.

#### Three things that will cost a day each if nobody says them

1. **`gamescopectl screenshot` DOES NOT CAPTURE EITHER OVERLAY PLANE.** Not with
   the default type 1, and not with type 2, `all_real_layers`, whose own summary
   says "the game + overlays". A capture taken with a working overlay on the
   screen comes back showing only the game. **Most of a session was spent
   chasing an overlay that was on the television the whole time**, and it ended
   because MMagTech looked up and said the gears were in the top left. There is
   no capture path that shows this. **Photograph the screen, or look at it.**

   This is the same lesson as "judge the look only on the A9", arriving in a new
   costume: the instrument was lying, and it was lying *plausibly*.

2. **Both slots are read only from the ROOT Xwayland context** —
   `pFocus->externalOverlayWindow = root_ctx->focus.externalOverlayWindow`. A
   window on any other server carries the atom correctly and is silently never
   consulted. With `--xwayland-count 2` the game lands on the root server, so
   the overlay must go there too and not on the second one.

3. **The atoms must be set BEFORE the window maps.** gamescope classifies a
   window when it maps; setting them afterwards sets a flag on a window nothing
   re-examines. There is a way to force the rescan — any property-notify on
   `_NET_WM_WINDOW_OPACITY` makes `handle_property_notify` walk `ctx->list` and
   reassign the overlay unconditionally — but that is a lever for testing
   somebody else's program, not a design.

**And the overlay is painted `NoScale`**, at its own pixel size, never stretched
to the output. The frontend would have to render its menu at the panel's full
resolution itself. glxgears' 300x300 window landing in a corner of a 4K screen
is what that looks like when you get it wrong.

#### What this does NOT answer

- **Nothing has been built.** This is one test program and a stand-in game. The
  frontend has not been split, PCSX2 has not been given a window, and no
  emulator has run this way.
- **The overlay has not been drawn by our own renderer.** The probe is XPutImage
  and a flat pattern. The frontend's menu is signed-distance fields in a GLES
  context and has never been asked to render into a transparent surface.
- **Nobody has measured what it saves.** The 6.1 ms is what the CURRENT path
  costs; the compositing path's own cost has not been measured at all. It should
  be near nothing — the emulator presents directly and gamescope blends one
  extra plane — but *should be* is how this project has been wrong before.
- **The headless backend does not do it**, and that is worth knowing because it
  is where the dev loop lives. An opaque fullscreen overlay makes no difference
  to a headless capture. Whether that is headless having no planes or the same
  capture blindness as item 1 was not separated out, because the DRM answer
  arrived first and made it moot.

#### TWO PATHS ARE ACCEPTED — MMagTech, 2026-09-21. THE CONDITION IS THE POINT

The price of this route was never the atoms. It is that the console would have
**two ways of getting a game onto the screen**: the twenty-one libretro cores
rendering into a texture the frontend owns, and the standalone emulators
presenting for themselves with our menu blended on top.

**MMagTech's call: *"i dont mind if its two paths so long as they resemble each
other closely."*** That settles the architecture question and replaces it with a
design constraint, which is the harder and more useful half.

**What "resemble each other closely" has to mean in practice**, so that this is
a testable requirement rather than a sentiment:

- **One menu, one set of code.** Pause, Save state, Exit to Home are drawn by
  the same renderer from the same scene, whether the game underneath came from
  our own texture or from another window's plane. If the composited path grows
  its own copy of the menu, the constraint has been broken no matter how similar
  they look.
- **The same behaviour at the same moments.** The menu opens on the same button,
  takes the pad at the same instant, dims the game the same amount, and ducks
  the audio the same way. A PlayStation 2 game and a Mega Drive game should be
  indistinguishable to the person holding the pad.
- **The same save-and-quit.** Exit to Home syncs the save on the way out on both
  paths, or the second path is a trap.
- **The difference is confined to where the picture comes from.** Anything that
  leaks above that line — different menu, different timing, different exit — is
  the failure this constraint exists to prevent.

**THE MENU IS NOT THE MAINTENANCE RISK, AND AN EARLIER VERSION OF THIS SECTION
IMPLIED IT WAS.** MMagTech pushed back: *"Its really just a mneu that is pretty
much the same between the two paths doesnt seem like much maintanance. Am i
missing something"* — and he is right. The menu is one scene, one renderer, one
set of code, drawn into a different surface. That is not two things to maintain.

**THE ONE REAL DIFFERENCE IS WHAT "PAUSE" MEANS**, and it is small, specific and
worth naming so it does not get discovered late:

- **Today, pausing is free.** The frontend stops calling `retro_run` and the
  game genuinely stops, because the console is the thing driving it.
- **Composited, the emulator keeps running unless told to stop.** This was
  visible in the experiment itself: the overlay held the pad and **the cube kept
  spinning**. PCSX2 runs on its own thread and does not care that a menu is up.

So Pause, Resume, Exit to Home and save-and-quit each need an explicit "and also
tell the emulator" that the current path gets for nothing. **That is a handful of
lifecycle calls, not a second menu.**

The drift worth guarding against is therefore narrow: keep the menu and its
behaviour genuinely shared — one scene, one input handler — so that the only
thing either path implements for itself is *where the picture comes from* and
*how the emulator is told to stop and start*.


#### WHAT IT SAVES, MEASURED — 2026-09-21

Run the same game at the same upscale, uncapped, twice: once reading the frame
back through the CPU as the console does today, and once with
`CABINETOS_PS2_NO_READBACK`, which skips exactly the work a composited PCSX2
would not do. Burnout 3, warm shader cache, on the A9. 4x was run twice and
reproduced within 3%.

| Upscale | readback on | readback off | throughput saved |
|---|---|---|---|
| 3x | 3.36 ms/frame | 3.25 ms/frame | **0.11 ms — free** |
| 4x | 5.08 / 5.21 ms | 3.21 / 3.25 ms | **~1.9 ms** |
| 6x | 13.16 ms | 4.17 ms | **~9.0 ms** (76 → 240 fps) |

**AT 3x THE READBACK IS FREE, AND THAT IS THE MOST USEFUL NUMBER HERE.** It
*measures* 1.95 ms and *costs* 0.11 ms, because it overlaps with the emulator's
other threads. So for PlayStation 2 at the upscale this project already calls the
sweet spot, compositing buys nothing at all.

**THE COUNTER OVERSTATES ITS OWN COST UNTIL IT DOES NOT.** At 4x it reports
~5 ms and removing it buys ~1.9 ms. At 6x it reports 13 ms and removing it buys
9 ms. The overlap absorbs a fixed amount and then the cost lands squarely on the
critical path — a cliff, not a curve, and 4x sits on the edge of it, which is
consistent with the stutter MMagTech felt there.

**WITHOUT THE READBACK, THE UPSCALE IS NEARLY FREE.** 3.25 ms at 3x, 3.21 ms at
4x, 4.17 ms at 6x. With it, 4x to 6x takes the emulator from 188 fps to 76.
Nearly the whole cost of a high upscale on this console is our picture path, not
the emulation and not the rendering.

**SO THE CASE IS NOT "PLAYSTATION 2 TODAY", AND SAYING OTHERWISE WOULD BE
OVERSELLING IT.** It is true 4K — 5x is the first genuinely 4K value on a
3840x2160 panel and that is past the cliff — and it is the systems that do not
exist yet. **MMagTech made that argument before the numbers came in and the
numbers support it better than they support the PS2 one:** *"you have to remeber
this could also benefit other more demanding cores we have yet to built like
ps3, switch, wii u and xbox."*

The reason is in the measurement rather than beside it. **The readback is free at
3x precisely because PCSX2 is running three times faster than realtime and has
slack to hide it in.** An emulator running at 100% has no slack. Lining the
systems up by pixels per frame, a PlayStation 3 at its own native 1080p pushes
about what a PS2 pushes at 3x — and pays for it with none of PS2's headroom.

| | pixels/frame | PS2 equivalent |
|---|---|---|
| PS2 native | 287k | 1x |
| Switch 720p | 922k | ~2x |
| PS3 1080p | 2.07M | ~3x |
| 4K | 8.3M | ~6x |

#### THE MENU RENDERS CORRECTLY ONTO NOTHING — 2026-09-21

`cabinetos-frontend --overlay-test` puts the real pause menu up as a gamescope
overlay over a game the console did not draw. Run on the A9 over vkcube,
MMagTech watching: *"everything looks pretty solid except the menu itself is not
glass. I can see the game around but not throught the menu."*

**THE RISK THAT COULD HAVE KILLED THE ROUTE IS GONE.** Text and panel edges are
clean, with none of the dark haloing a renderer produces when it gets
premultiplied alpha wrong on a transparent surface. The scrim dims the game
correctly. **It needed no shader work at all** — `glBlendFuncSeparate(..., GL_ONE,
GL_ONE_MINUS_SRC_ALPHA)` was already right, so the whole change is two clears
going from alpha 1 to alpha 0 and one full-screen black fill being skipped.

**THE ONE CASUALTY IS THE GLASS PANEL, AND IT IS A REAL DIFFERENCE.**
`drawGlass` blurs what is behind it by sampling the console's OWN scene texture.
On this path the game is on another plane and is not in that texture, so there is
nothing to blur and the panel comes out flat.

**gamescope's own blur was tried and DID NOT WORK.** `GAMESCOPE_BLUR_MODE` set to
1 and to 2 on the root window, with radius and fade duration, with composition
forced. `composite_debug` confirmed gamescope really was compositing — MMagTech
saw the markers — and the blur shaders are in the build. Why it does not apply
was not established, and nobody should assume it is unavailable on the strength
of this. It was abandoned rather than solved.

**THE PROMISING ROUTE INSTEAD IS ONE GRAB AT THE MOMENT OF PAUSE, and the
ingredient is proved.** A base-plane screenshot taken WHILE our overlay is on
screen comes back as the game alone, clean, at full output resolution — verified
2026-09-21. So the console can take one grab when the menu opens, blur it itself,
and use it as the panel's backdrop for as long as the menu is up. The game is
paused, so a still is correct rather than a compromise, and the panel already
fades in over 350 ms, which is room to do it in. One grab per pause instead of
sixty a second, in our own code, preserving the look on both paths.

**That the thing which wasted most of a session — screenshots not capturing the
overlay planes — is exactly what makes this possible is worth a moment's
reflection before the next instrument is trusted or discarded.**

The reproduction is `tools/gamescope-overlay-test.sh` and `tools/overlay-probe.c`.

### 25. Save states, and whether the modern systems should have them at all
**Raised by MMagTech, 2026-09-21, while asking whether PlayStation 2 saves work.
DECIDED for PS2 and everything after it. OPEN for the cartridge era.**

#### What prompted it

`Core::stateSize()` returns 0 for PlayStation 2 and says so honestly, because
PCSX2 keeps its states as its own slot files keyed by disc serial and CRC rather
than as a buffer this console can hold. That was written down as work owed —
"whatever this console does there is new work" — and the obvious next step was
to go and find out whether PCSX2 can serialise to memory.

**The right question turned out to be the one before it.** MMagTech: *"should
these more modern system even have save and load states or just the memory
cards"*, and then: *"that seems like a nightmare to plan for and something i
dont even think modern systems allow you to do."*

**He is right on the second point and it is the whole argument.** No console
made in the last twenty years exposes a save state. The Switch suspends, the
PS5 has Rest Mode, the Xbox has Quick Resume. None of them offers a slot to save
into. A pause menu with "Save state" on it is an emulator's idea, not a
console's, and this project's whole premise is that it is building a console.

#### DECIDED: no visible save states for PlayStation 2, GameCube, or anything after

Three reasons, in order of how much they cost if ignored:

1. **THE STATE BREAKS WHEN THE IMAGE UPDATES.** A PCSX2 state is tied to the
   emulator build as well as to the disc. Push an image, PCSX2 moves, and
   everyone's states silently stop loading. **That is data loss on this
   project's release schedule rather than on the player's** — and it is the kind
   that is discovered long after the update, by someone who cannot get back to
   what they had. A memory card survives an emulator upgrade. This alone settles
   it.
2. **These consoles have real save systems and every game uses them.** A PS2 or
   GameCube game has save points. The problem save states were invented for —
   a cartridge with no battery, where a state is the only way to stop mid-level
   — does not exist here.
3. **States do not travel and the library is meant to.** A memory card opens on
   the Mac and on an Apple TV. A PCSX2 state from this build does not open
   anywhere else, so shipping it through RomM would put a file in the library
   that only one machine can read.

**So item 2 on the PlayStation 2 list is closed by DELETION rather than by
work**, which is the outcome this project keeps arriving at when MMagTech
simplifies something.

#### WHAT PEOPLE ACTUALLY WANT IS RESUME, WHICH IS A DIFFERENT FEATURE

The thing a person wants on a television is not a slot. It is: turn the console
off mid-race, come back tomorrow, still be mid-race. **That is suspend and
resume, and it is exactly what the modern consoles named above do instead.**

The mechanism is the same — a snapshot of the machine — and the product is not:

| | Save state | Suspend |
|---|---|---|
| How many | Slots the player manages | Exactly one, per game, invisible |
| How long it lives | Forever, and is curated | Until next launch |
| If an update invalidates it | A save is lost | Last night's resume point is lost |
| In the menu | Two items | None |

**That difference in lifetime is what makes the update problem tolerable.**
Losing a resume point is an annoyance; losing a save someone deliberately made
is a fault.

#### STILL OPEN: the twenty-one cartridge-era cores, where states DO work today

"Save state" and "Load latest state" are two of the four items in the pause
menu right now, and for a Mega Drive or a NES game they are the right answer —
there a state is often the only way to stop mid-level, and it is the idiom every
emulator frontend on every platform uses.

**So the menu would offer different items on different systems, and that needs
deciding rather than assuming.** The defensible principle is *states exist where
the system has no save of its own*, which is roughly the cartridge/disc line —
but it is per-GAME rather than per-platform in truth, and a rule that is nearly
right is how a console ends up feeling arbitrary.

**Do not resolve this inside a PlayStation 2 change.** It touches 21 working
cores, the pause menu's shape, and what Cabinet's other platforms expect to find
in a RomM row. It belongs with open question 23 and the UI pass, and all three
are really one conversation about what this console is rather than three
separate features.
