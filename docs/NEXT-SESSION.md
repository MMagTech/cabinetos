# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-16, late:** `latest` is **2026.10.14.5** (built from 72c0c4b,
tagged in git). The A9 (Wi-Fi, **192.168.1.109**; Tailscale cabinetos,
100.67.82.30) is on the testing channel; check `/usr/lib/os-release` for
what it runs. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

**The restructure (#398, "Audit the code and restructure it for
contributors (main.cpp is 20,000 lines)") runs unattended to the end**
(MMagTech, 2026-10-16, 8:48 PM: "Unattended to the end, your calls; push to
testing and merge into restructure yourself"). Moves 1 to 6 are merged into
the long-lived `restructure` branch; move 6, batch (h1), is #418 ("Restructure
move 6: batch (h1), screens.cpp into a file per screen and the toolkit's
pieces into their own files (#398)"). Move 6's session (a side session of
local_9923e863) goes on to batch (h2), applying the generators four parallel
prep sessions wrote (`~/Documents/Development/cabinetos-audit/scripts/move7/`
core, standalone, rest), then batch (i) (`scripts/move7/romm`, the RomM
client split and the thread rule; it does not wait for Part 5). Each batch
gets the machine checks (builds, line checks, the headless suite, the loop
with its sound line) and its own testing image, kept for bisecting, and is
merged into `restructure` without a TV round of its own.

**MMagTech gets one checklist at the end**, on the final image: the install
(Settings > System update asks for his PIN; the assistant never types a PIN
or a password), the TV items of (h1), (h2) and (i) with Part 6 section 27's
full list, the PIN back on, then his "merge" of `restructure` into main, the
only step that reaches players.

## Next

- If move 6's session is still at work: nothing; it reports. If it stopped,
  pick up from `~/Documents/Development/cabinetos-audit/moves-log.md` (the
  last entry says where), the hand-overs in `scripts/move7/*/HANDOVER.md`,
  and `move7-prompt.md`.
- After the merge to main: Parts 4 and 5 of the audit (report only; quote
  the cost before any agent wave), then the contributor guide, then the
  faults the moves left as they were, each in its own pull request.

**What the moves have taught** (testing.md has them in full): run every
headless test through `tools/romm-readonly-relay.py`; run the base binary
twice to show what timing alone changes, and swap the scratch roots before
blaming a binary; a test game on the console ends inside a minute by
`--restore`; `rsync --delete` a tree's sources onto the A9 before its loop
deploy; give podman an absolute folder, never `$PWD`, under `set -e`. New in
move 6: two suite runs side by side need their own relay ports, and one
fixed path is shared (`/tmp/cabinetos-manual.pdf`), so start them minutes
apart; a capture timed by `--frames` shows a different fade when the machine
is busy, so rerun a large difference alone before reading anything into it.

**Owed by MMagTech, at the checklist or after:** Part 6 section 29 decisions
2 to 6 (which faults get issues, protectRomId 0, folders for the files, the
test instruments, the names); #408 ("Next session: Part 6 of the #398 audit
is posted; the moves begin with batch (a)") is still open against main; the
merged move branches to delete (listed in the checklist).

## Waiting, not for the next session unless MMagTech raises them

- #401, "RomM 5.4.0: a game's page cannot read its time played without
  devices.read or a device_id" and #402, "A one-file game whose file name
  begins with # or ? fails to download (the name is cut before the server
  sees it)".
- #404, "The launch worker writes the job's entry path while the frame
  thread reads it"; #405, "favorites.json is written from two threads through
  the same temporary file with no lock"; #406, "Four background threads are
  never stopped at exit (pairing, remove and sign out, reconnect, drive
  backfill)".
- #329, "Release notes for every update, and a way to see them from the
  console"; #380, "The console follows any web address in RomM's data, with
  the RomM sign-in attached"; #381, "Remote Play: stream with no screen
  connected"; the Bazzite base update #371.
- #387, "A cover fixed on RomM shows only after the console restarts".
- #390, "Headphones don't take over after a Remote Play session (waiting on
  Sunshine)": MMagTech writes the Sunshine report in his own words.
- #391, "Headphones in at start get full volume"; #393, "Power off and
  Restart cut the picture abruptly": a fade only; no sound, ever.
- #181, "Updates and DLC as separate files in RomM (Switch, PS3, Xbox 360)".
- #411, "Connecting Moonlight can start the game Home has focused" and
  #412, "The log names the previous emulator when a PS2 game starts
  ("running Beetle Saturn")".
- #417, "The diagnostic report stops downloading at about 2.5 MB (its ten
  seconds are counted as 250 ms per wait)".
- deletableman (#341, #376): nothing new from him.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
