# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-16:** `latest` is **2026.10.14.5** (built from 72c0c4b, tagged in
git). The A9 (Wi-Fi, **192.168.1.109**; Tailscale cabinetos,
100.67.82.30) is on the testing channel and runs **2026.10.16.5**, acting as
claire after move 5's Restart test (switch back from the chip). RomM's
documentation together with RomM's behaviour is the source of truth for
anything on RomM's data. RomM was asked to send its `version` tag
(rommapp/romm#5243); nothing to do until it does.

**Features and bug fixes are paused for the restructure (#398, "Audit the
code and restructure it for contributors (main.cpp is 20,000 lines)").**
Every batch is a pull request into the long-lived `restructure` branch;
`latest` stays at 2026.10.14.5 until one final pull request to main, judged
with the full checklist on its testing image (Part 6 section 26).

**Moves 1 to 5 are done: main.cpp is 192 lines**, main() and run() (the
command line, the start-up, the frame loop, the teardown, as a list of
calls). Move 1, batch (a): #409. Move 2, batch (b): #410. Move 3, batch (c):
#413. Move 4, batches (d) and (e): #414. Move 5, batches (f) and (g): #415
("Restructure move 5: batches (f) and (g), the game in play, the menus, input
and the frame loop out of main.cpp (#398)"), testing images **2026.10.16.4**
(b7a58d3, the (f) half) and **2026.10.16.5** (20edb58, both halves). The
frame loop is frame.cpp, input is input.cpp, the game in play is play.cpp,
pace.cpp and launch.cpp (pumpLaunch's stages over a LaunchContext). The
record, the check results and the scripts are in
`~/Documents/Development/cabinetos-audit/moves-log.md` and `scripts/move1/`
to `scripts/move5/`.

## Next

**Batches (h1) and (h2), in 2 or 3 sessions, then (i) alone** (decided
2026-10-15). (h1): screens.cpp into seven files, screens.h into the protocol
and six headers, the shaders out of ui.cpp, qrtexture.h, motion.h, overlay.h
and card.h out of design.h, imagecodec out of image.cpp. (h2): core.cpp,
vkhost.cpp, standalone.cpp, setup.cpp, achievements.cpp, storage, drives,
catalog and the small splits, one big file per pull request. The lists, the
must-keep items and the checks are in
`~/Documents/Development/cabinetos-audit/verifiers/batch-h1.json` and
`batch-h2.json`, and in `scripts/part6-batches.md`. No prompt is written for
them yet. They touch no line of main.cpp's moves; the method (every cut
found by its text, both ends asserted, a generator that reproduces each
commit) carries over from `scripts/move5/gen5.py`.

**What the moves have taught** (testing.md has them in full):
- Run every headless test through `tools/romm-readonly-relay.py`; a game
  test that may save or play over a minute goes in a scratch storage root
  with the relay, on the TV too (`tools/ui-loop.sh --env
  CABINETOS_ROMM=127.0.0.1:16007 --args "--storage-root ..."`).
- Run the base binary twice to show what timing alone changes, and when a
  difference looks systematic, swap the scratch roots: in move 5 it followed
  the root (an owed state with a picture), not the binary.
- A test game on the console ends inside a minute, by `--restore`, never by
  a kill.
- The loop's rsync keeps files it does not know: before deploying an older
  tree after a newer one, `rsync --delete` its sources onto the A9.
- Never chain a podman `:Z` run after a check with `;`, and give it an
  absolute folder, never `$PWD`: move 5 relabelled part of the A9's home that
  way (repaired with restorecon).

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
- #417, "The diagnostic report stops downloading at about 2.5 MB (its ten
  seconds are counted as 250 ms per wait)": found in move 5's TV round,
  older than the moves, in the Later milestone.
- deletableman (#341, #376): nothing new from him.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
