# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `CONTRIBUTING.md`, then `docs/ARCHITECTURE.md`.
The specification is `docs/PROJECT.md`; its index, at the top, gives
every section and every numbered question with where it ended. Name every issue and pull request by
its title as well as its number, every time. Replies to reporters are in
MMagTech's voice ("I"), ask them to test, and never claim a fix nobody
reproduced. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

## Where things stand

**2026-10-18:** `latest` and `testing` are **2026.10.18.6** (revision
c1f716d, digest sha256:78ec67bf...a843), #508 ("PS1 gets a DualShock, DS
touch on the right stick, DS 3D upscaled (#345, #374, #219)"), merged on
his go after his TV check and promoted; #345, #374 and #219 are closed. PS1
games get a DualShock (device 517); DS touch is on the right stick (R3
touches); melonDS is built with its OpenGL renderer and the core host gives
a core that asks for desktop OpenGL a context of its own beside the UI's
(`core_hwrender.cpp`), at Balanced 4x and Quality 6x (Performance keeps the
software renderer). The A9 is on the testing channel; its PIN is off until
he turns it back on. The machines' addresses are in his local notes, which
CLAUDE.md loads; the test tools read them from the environment
(`CABINETOS_A9`, `CABINETOS_KEY`, `CABINETOS_VM`, `CABINETOS_RELAY_UP`).

**The ecosystem, from MMagTech 2026-10-18:** CabinetOS is the console at
home; **Cabinet Go** (the old Cabinet iOS app, renamed, at
github.com/MMagTech/cabinet-go) is the pocket version that plays only the
handheld systems. It is not finished and follows at a later, unknown date.
Every other Apple app is abandoned. The docs that call Cabinet "the Apple
TV app" (CONTRIBUTING.md, docs/CABINET.md, PROJECT.md's banner, README.md,
the site) need reviewing against this.

**Steps 1 to 4 of the #398 plan are done** (#398, "Audit the code and
restructure it for contributors (main.cpp is 20,000 lines)"): the guide
(#426), the docs tidy (#427), and the faults: every recorded fault is either
fixed in a pull request into `faults` (#429 to #498) or in one of 21 issues
grouped by area in the Later milestone (#471 to #493). The list of every
fault with its verdict is `faults-list.md` in the audit folder (its path is
in the local notes).

## Next

1. **FSR for Xbox 360, an experiment** (MMagTech, 2026-10-18: "id like to
   experment with fsr on 360 next before moving into the rest"). Xbox 360
   renders 720p at every Picture level (`quality.cpp`, `xenia()`: 2x could
   not hold 30 fps on the A9). Xenia Edge at our pin (a7c39fa,
   `build_files/install-xenia.sh`) has FSR 1.0 built in,
   `--postprocess_scaling_and_sharpening=fsr`, and also **CAS and its own
   sharpness settings; research every one of them at the pin before choosing**
   (MMagTech: "Both should be researched so we implement the feature
   correctly"). Xenia's own flags only: gamescope's FSR cannot reach the
   libretro cores and cannot stop short of the screen's size. Then a before
   and after on the TV, and his call. Lessons: docs/lessons/emulators.md.
2. **#401, "RomM 5.4.0: a game's page cannot read its time played without
   devices.read or a device_id"**, so that his server can move to RomM 5.4
   (he asked when, 2026-10-18; the advice: after #401 ships, with a
   database backup first). The issue lists two options for him to choose;
   the audit recommends the first, sending `device_id` from
   `GET /api/users/me` on that one request, which needs no console to pair
   again.
3. **The automatic checks report only until 2026-10-25**, then fail a pull
   request: RomM paths outside the client, docs naming missing files, new
   compiler warnings in touched files, the self-tests. Read their first
   week's reports before that date and fix what they find, so nothing turns
   red by surprise (CONTRIBUTING.md, "What CI checks").
4. **A priority list of every open issue, then work through it**
   (MMagTech, 2026-10-18). The Later milestone holds 66, including
   the three found at the TV on 2026-10-18, ranked with the rest and no
   sooner: #509, "A mouse arrow shows over Home and games (a PS5 pad's
   touchpad moves it); hide it, as question 16 decided"; #510, "No sound
   after the TV comes back from standby: the console's stream misses every
   PipeWire cycle (not #228)"; #511, "Every update downloads about 310 MB
   that did not change (the boot splash and OS layers differ in every
   image)". For the
   faults step's 21 (#471 to #493), the view he was given: fix the ones a
   player could hit, close the developer-only ones with "found by reading,
   reopen on a report"; show him the list before closing anything, ask his
   decisions as one short list, keep the TV checks for one sitting at the
   end. Player reports and features come first.
5. **Review the docs for Cabinet Go** (above).
6. **The monthly health check** starts after item 3: the checks' reports
   (warnings, long files, unused code, PROJECT.md's index) and the Later
   milestone, once a month.

## Waiting, not for the next session unless MMagTech raises them

- The 21 issues from the faults step (#471 to #493, each titled by its
  fault), and the audit's older ones: #402,
  "A one-file game whose file name begins with # or ? fails to download (the
  name is cut before the server sees it)"; #404, "The launch worker writes
  the job's entry path while the frame thread reads it"; #405,
  "favorites.json is written from two threads through the same temporary
  file with no lock"; #406, "Four background threads are never stopped at
  exit (pairing, remove and sign out, reconnect, drive backfill)" (its
  Add-account part is fixed in #470); #380, "The console follows any web
  address in RomM's data, with the RomM sign-in attached".
- #487, "CI, the bots and the tools: faults left", needs one thing only
  MMagTech can do: the BASE_UPDATE_TOKEN secret, without which bot pull
  requests start no checks.
- #329, "Release notes for every update, and a way to see them from the
  console"; #381, "Remote Play: stream with no screen connected"; #387, "A
  cover fixed on RomM shows only after the console restarts"; #390,
  "Headphones don't take over after a Remote Play session (waiting on
  Sunshine)"; #391, "Headphones in at start get full volume"; #393, "Power
  off and Restart cut the picture abruptly"; #181, "Updates and DLC as
  separate files in RomM (Switch, PS3, Xbox 360)"; #411, "Connecting
  Moonlight can start the game Home has focused"; #412, "The log names the
  previous emulator when a PS2 game starts ("running Beetle Saturn")";
  #417, "The diagnostic report stops downloading at about 2.5 MB (its ten
  seconds are counted as 250 ms per wait)"; #422, "Primal Rage (PS1) will
  not start: PCSX-ReARMed says its CHD is not a valid CD image"; #506, "A
  PS2 disc PCSX2 cannot open starts as if it were playing, with no message"
  (found headless while testing #505; the case is Destroy All Humans! 2,
  whose CHD on his server was made with `createcd` from a DVD).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
