# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-15:** `latest` is **2026.10.14.5** (built from 72c0c4b, tagged in
git). The A9 (Wi-Fi, **192.168.1.109**; Tailscale cabinetos,
100.67.82.30) is on the testing channel. RomM's documentation together with
RomM's behaviour is the source of truth for anything on RomM's data. RomM
was asked to send its `version` tag (rommapp/romm#5243); nothing to do
until it does.

**Features and bug fixes are paused for the restructure (#398, "Audit the
code and restructure it for contributors (main.cpp is 20,000 lines)").**
Every batch is a pull request into the long-lived `restructure` branch;
`latest` stays at 2026.10.14.5 until one final pull request to main, judged
with the full checklist on its testing image (Part 6 section 26).

**Moves 1 and 2 are done.** Move 1, batch (a): #409 ("Restructure move 1:
dead code out, the probes, self-tests and helpers out of main.cpp (#398)").
Move 2, batch (b): #410 ("Restructure move 2: the launch code out of
main.cpp into launch.h, launch.cpp and launch_routes.cpp (#398)"), testing
image **2026.10.15.2** (88b5ed4), merged into `restructure` on
2026-10-15. main.cpp is 14,460 lines. The record, the check results and
the scripts are in `~/Documents/Development/cabinetos-audit/moves-log.md`,
`scripts/move1/` and `scripts/move2/`.

## Next

**Move 3 is batch (c), the App struct, alone.** Its prompt is
`~/Documents/Development/cabinetos-audit/move3-prompt.md`. It gets the full
TV checklist, so it is not unattended unless MMagTech says so, and it
measures the exit after Add an account first (Part 6 section 27, item 4b).

**The batches after it (decided 2026-10-15, MMagTech: "proceed as you see
fit for the end goal"):** (d) with (e) in one TV round; (f) with (g) in one
TV round, stopping for a separate judgement of (f) if its machine checks
show anything odd; (h1) and (h2) in 2 or 3 sessions; (i) alone. Every half
gets its own testing image, so a fault seen on the TV can be narrowed down
by switching the A9 to the half-way image. About 6 to 8 sessions, against
11 to 14 one batch at a time; the reason is fewer, more careful TV rounds,
not time.

**What the moves have taught** (testing.md has them in full):
- Run every headless test through `tools/romm-readonly-relay.py`.
- A `--frames` capture of a launch is taken before the game loads; use the
  disc rig's `shot` and a time limit.
- A core stays mapped after a game is left; `scripts/move2/idle.sh` reads
  the journal for that case.
- The testing push needs MMagTech's word in the session that pushes; a
  relayed okay was refused by the permission check.

**Owed by MMagTech:**
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
- #411, "Connecting Moonlight can start the game Home has focused" and
  #412, "The log names the previous emulator when a PS2 game starts
  ("running Beetle Saturn")": both found in move 2's checks, both older than
  the moves, in the Later milestone.
- deletableman (#341, #376): nothing new from him.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
