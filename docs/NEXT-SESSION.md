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

**2026-10-18:** `testing` is **2026.10.18.3** (revision fe5a914, digest
sha256:162f2063...cf80a), #501 ("Games kept as one file in a RomM folder
start again (#400, #416)"): a game RomM keeps as one file inside its own
folder, which RomM's own upload of a manual or soundtrack makes, downloads
only the game file. Proven on the A9 before and after through the
read-only relay (the PR body). His merge promotes it. The faults step
(#500, 2026.10.18.2) is merged. #416's other half, which BIOS PCSX2 starts
(it takes the first file in `bios/`; RetroArch and Batocera do the same),
waits for the player to try #501. The A9 is on the testing channel; its
PIN is off until he turns it back on. The machines' addresses are in his
local notes, which CLAUDE.md loads; the test tools read them from the
environment since #456 (`CABINETOS_A9`, `CABINETOS_KEY`, `CABINETOS_VM`,
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

1. **The Bazzite base update, in a session of its own, first** (MMagTech,
   2026-10-18): #371, "Bazzite base → 44.20261012 (relevant)", open since
   2026-10-12; check first whether Bazzite has a newer one. Alone on
   `testing`, so a fault is the base's. Checks only for the packages that
   changed (log checks by the session, a few eye and hand checks by him).
   It shows no checks because `BASE_UPDATE_TOKEN` is not set (#487,
   SCRIPTS-13); a push to its branch by the session starts them. It needs
   a changelog entry for players before merging.
2. **A priority list of every open issue, then work through it**
   (MMagTech, 2026-10-18). The Later milestone holds 65. For the faults
   step's 19 (#471 to #493), the view he was given: fix the 13 a player
   could hit (about 60 small fixes, 3 or 4 sessions side by side), close the
   developer-only ones (CI and bots, developer tools, stale comments, dead
   code, races found by reading, "not now") with "found by reading, reopen
   on a report"; ask his decisions as one short list first and keep the TV
   checks for one sitting at the end. Not yet agreed: show him the list
   before closing anything. Player reports and features come first.
3. **Review the docs for Cabinet Go** (above).
4. **The automatic checks report only until 2026-10-25**, then fail a pull
   request: RomM paths outside the client, docs naming missing files, new
   compiler warnings in touched files, the self-tests. Read their first
   week's reports before that date and fix what they find, so nothing turns
   red by surprise (CONTRIBUTING.md, "What CI checks").
5. **The monthly health check** (step 5) starts after that: the checks'
   reports (warnings, long files, unused code, PROJECT.md's index) and the
   Later milestone, once a month.

## Waiting, not for the next session unless MMagTech raises them

- The 21 issues from the faults step (#471 to #493, each titled by its
  fault), and the audit's older ones: #401, "RomM 5.4.0: a game's page
  cannot read its time played without devices.read or a device_id"; #402,
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
  not start: PCSX-ReARMed says its CHD is not a valid CD image"; the
  Bazzite base update #371, "Bazzite base → 44.20261012 (relevant)".

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
