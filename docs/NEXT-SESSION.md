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

**2026-10-18:** `latest` and `testing` are **2026.10.18.5** (revision
0c3d33a, digest sha256:092aaf81...b0846a), #505 ("PS2 games start on the
BIOS for their region (#416); the base check goes monthly"), merged on his
go after his TV check and promoted. A PS2 game now starts on a BIOS of its
own region, from the disc code RomM reads (`title_id`); with none for its
region, the newest USA BIOS; with no USA one, the newest there is
(`frontend/ps2/CabinetPS2Bios.h`). On the A9 Burnout 3 took the USA BIOS,
Homura the Europe one and Gradius V the USA one. #416 is closed with a
comment that asks the reporter nothing unless it is still wrong (MMagTech
wants no back-and-forth). `base-update.yml` now runs
on the 1st of each month (next 2026-11-01); between runs a base moves early
only for a player's fault a newer Bazzite fixes, or a security hole
(docs/PROJECT.md, "Staying current with Bazzite"), and one that moves to
Fedora 45 goes alone on `testing`, as #503 did. The A9 is on the testing
channel; its PIN is off until he turns it back on. The machines' addresses
are in his local notes, which CLAUDE.md loads; the test tools read them
from the environment (`CABINETOS_A9`, `CABINETOS_KEY`, `CABINETOS_VM`,
`CABINETOS_RELAY_UP`).

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

1. **Three things players will notice, in one testing image and one TV
   sitting** (MMagTech, 2026-10-18: user-facing improvements next). Read
   docs/lessons/emulators.md, and compare each with RetroArch's and
   Batocera's build flags and options before any TV test.
   - **#345, "PS1: plug in a DualShock, not the digital pad (sticks and
     rumble)".** The research and the proposal are on the issue: the
     DualShock (device 517) on every port for every PS1 game, the core's
     defaults kept. Its TV check: a digital-only game, a DualShock game and
     a rumble game (check the titles against Redump first).
   - **#374, "Nintendo DS: the touch screen probably does nothing; set it
     to the right stick".** `melonds_touch_mode` to `Joystick` in
     catalog.cpp's melonDS options; the right stick moves the pointer and
     R3 taps. TV: a game that needs a tap, before and after. #288 ("Nintendo
     DS: a controller's touchpad as the touch screen") stays for later.
   - **#219, "DS: built without its graphics renderer, so it cannot
     upscale".** The big one: melonDS asks for a desktop OpenGL 3.1 core
     context and the core host serves only OpenGL ES and Vulkan
     (core.cpp, `canServe`). So the host learns desktop GL first, then
     `HAVE_OPENGL=1` in cores/build-core.sh (and its stale comment there),
     then melonDS's internal resolution joins the picture-quality dial.
     OpenGL is allowed here because melonDS has no Vulkan renderer
     (MMagTech on the issue, 2026-10-12). Measure on the A9. If it turns
     out large, tell him before the other two wait on it.
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
   (MMagTech, 2026-10-18). The Later milestone holds about 65. For the
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
