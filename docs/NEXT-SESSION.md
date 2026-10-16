# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-16:** `latest` is **2026.10.14.5** (built from 72c0c4b, tagged in
git). The A9 (Wi-Fi, **192.168.1.109**; Tailscale cabinetos,
100.67.82.30) is on the testing channel and runs **2026.10.16.3**, acting as
claire after move 4's Restart test (switch back from the chip). RomM's
documentation together with RomM's behaviour is the source of truth for
anything on RomM's data. RomM was asked to send its `version` tag
(rommapp/romm#5243); nothing to do until it does.

**Features and bug fixes are paused for the restructure (#398, "Audit the
code and restructure it for contributors (main.cpp is 20,000 lines)").**
Every batch is a pull request into the long-lived `restructure` branch;
`latest` stays at 2026.10.14.5 until one final pull request to main, judged
with the full checklist on its testing image (Part 6 section 26).

**Moves 1 to 4 are done.** Move 1, batch (a): #409. Move 2, batch (b):
#410. Move 3, batch (c): #413. Move 4, batches (d) and (e): #414
("Restructure move 4: batches (d) and (e), the start-up, browsing, Settings
and its services out of main.cpp (#398)"), testing images **2026.10.16.2**
(c33c65d, the (d) half) and **2026.10.16.3** (231ed5d, both halves), merged
into `restructure` on 2026-10-16. main.cpp is 4,632 lines: main(), run()
with the frame loop, and batches (f) and (g)'s code. The start-up is a list
of calls in run() into startup.cpp and the feature files; thirty-two new
files hold browsing, Settings and the services. The record, the check
results and the scripts are in
`~/Documents/Development/cabinetos-audit/moves-log.md` and `scripts/move1/`
to `scripts/move4/`.

## Next

**Move 5 is batch (f) with batch (g), one TV round** (decided 2026-10-15):
stop and have MMagTech judge (f) alone first if the (f) half's machine checks
show anything odd. Its prompt is
`~/Documents/Development/cabinetos-audit/move5-prompt.md`. Each half gets its
own testing image. It is not unattended unless MMagTech says so.

**How move 4 cut, which (f) and (g) reuse** (`scripts/move4/gen4.py`): every
stage is generated from `restructure`'s own files, cumulative, every cut found
by its text with both ends asserted. A member function moves whole with its
leading comment; its declaration stays in app.h where move 3 put it. A
stretch of run() (or of a member function, as apply's branches were) becomes
a member function in its file and a call at the same point; the new functions
are declared in one block at the end of struct App, by file. Loop locals a
stretch reads are passed in (dt, sc, gameUp, ctx, the event); a `continue`
that ended an event becomes `return true` and the call `if (...) continue;`;
a `break` out of apply's switch becomes `return`. Lines another batch owns
stay in run() between the calls, so each line moves once.

**What the moves have taught** (testing.md has them in full):
- Run every headless test through `tools/romm-readonly-relay.py`.
- A `--frames` capture of a launch is taken before the game loads; use the
  disc rig's `shot` and a time limit.
- A core stays mapped after a game is left; `scripts/move4/idle.sh` reads
  the journal for that case.
- The testing push needs MMagTech's word in the session that pushes; a
  relayed okay was refused by the permission check.
- A pad left on during a suite run changes its output; note any pad in the
  console's journal before comparing two runs, and run the base binary twice
  to show what timing alone changes.
- A test game on the console ends inside a minute, by `--restore`, never by
  a kill: move 4 put a one-minute play of Tetris & Dr. Mario on MMagTech's
  RomM that way.

**Owed by MMagTech:**
- Whether to remove the one-minute Tetris & Dr. Mario play session move 4's
  loop put on RomM (2026-10-15, 11:28 PM); it is harmless and goes down the
  Recent shelf with his next game.
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
