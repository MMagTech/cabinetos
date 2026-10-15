# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-15:** `latest` is **2026.10.14.5** (built from 72c0c4b, tagged in
git). The A9 (Wi-Fi, **192.168.1.109**) is on the testing channel. RomM's
documentation together with RomM's behaviour is the source of truth for
anything on RomM's data. RomM was asked to send its `version` tag
(rommapp/romm#5243); nothing to do until it does.

**Features and bug fixes are paused for the restructure (#398, "Audit the
code and restructure it for contributors (main.cpp is 20,000 lines)").**
Every batch is a pull request into the long-lived `restructure` branch;
`latest` stays at 2026.10.14.5 until one final pull request to main, judged
with the full checklist on its testing image (Part 6 section 26).

**Move 1 is done:** batch (a), #409 ("Restructure move 1: dead code out, the
probes, self-tests and helpers out of main.cpp (#398)"), testing image
**2026.10.15** (d78f6bd), merged into `restructure` on 2026-10-15. 73 dead items removed; the probes, self-tests and every
helper above `main()` moved into 38 new files; main.cpp is 15,540 lines.
The record, the check results and the scripts are in
`~/Documents/Development/cabinetos-audit/moves-log.md` and `scripts/move1/`.

## Next

**Move 2 is batch (b):** launch.h, launch.cpp and launch_routes.cpp. Its
prompt is `~/Documents/Development/cabinetos-audit/move2-prompt.md`. It gets
the full TV checklist, so it is not unattended unless MMagTech says so: the
merge waits for his judgement of the TV items. Each later move takes its
prompt from Part 6 section 28 and its batch file the same way.

**What move 1 taught the moves** (testing.md has the first in full):
- Run every headless test through `tools/romm-readonly-relay.py`.
  `--sync-test` uploads a state as the active account when the core has a
  state tag, and RomM then moves the game to the front of Recent.
- Cut by the audit's line numbers with every boundary asserted, check every
  line before and after as a set, and commit in stages that each build
  (`scripts/move1/moves.py`, `stages.sh`); compare the two binaries' test
  outputs with `compare.py`; the five captures came out pixel-identical.

**Owed by MMagTech:**
- The test state move 1 left on RomM: Cannon Spike (rom 550), state id 128,
  uploaded at 6:43 AM on 2026-10-15, which also set the game's last played,
  so it leads Recent ahead of Raiden IV OverKill. Deleting the state and
  putting last played back are writes to his server, on his go. Until then,
  two more state saves on Cannon Spike rotate out his own state id 28.
- Part 6 section 29 decisions 2 to 6: which faults get issues, protectRomId
  0, folders for the files, the test instruments, the names.
- #408 ("Next session: Part 6 of the #398 audit is posted; the moves begin
  with batch (a)") is still open against main; this file supersedes it on
  the `restructure` branch.

**Settled and not to be reopened:** the client's thread rule is fixed in
batch (i), after Part 5 (MMagTech, 2026-10-14); the two PIN gates (decided
2026-10-07); #402 corrected; #404, #405 and #406 opened, fixed as their
files move or sooner on his call; Parts 4 and 5 come after the moves, then
the contributor guide.

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
- deletableman (#341, #376): nothing new from him.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
