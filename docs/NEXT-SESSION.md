# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-15:** `latest` is **2026.10.14.5** (built from 72c0c4b). The A9
(Wi-Fi, **192.168.1.109**) runs it on the testing channel. RomM's
documentation together with RomM's behaviour is the source of truth for
anything on RomM's data. RomM was asked to send its `version` tag
(rommapp/romm#5243); nothing to do until it does.

**Features and bug fixes are paused for the restructure (MMagTech,
2026-10-14).** Performance is not the reason: features must stop breaking
each other, and outside contributors must find their way.

## Next

**The audit's planning is complete (Part 6 posted 2026-10-14, 11:45 PM).** Parts 1, 2, 3 and 6 of #398, "Audit
the code and restructure it for contributors (main.cpp is 20,000 lines)",
are posted on the issue; Parts 4 (the scripts, services, image build, cores,
CI, workflows and tools) and 5 (the docs and the RomM map's refutation) come
after the moves, before the contributor guide (MMagTech, 2026-10-14).

**The moves begin.** Part 6 sets the order in ten batches, (a) to (i), with a
must-keep list and a check list per batch (`~/Documents/Development/
cabinetos-audit/verifiers/batch-*.json`, and Part 6 section 28 on the issue).
**Move 1 is batch (a)**: the dead-code removal, the probes and self-tests out
of main.cpp, and the helpers above main(); its prompt is
`~/Documents/Development/cabinetos-audit/move1-prompt.md`. It runs unattended
to the testing image and the machine checks, then merges its pull request
into `restructure` by itself (MMagTech's go, 10:58 PM and 11:20 PM on
2026-10-14, for this batch alone because its TV list is empty); the update
of the A9 and the smoke check are his. The Part 6 session set a timed
kick-off for 3:35 AM Eastern on 2026-10-15 that starts the move-1 session
by itself if the Mac stayed awake; if it did not fire, move 1 starts from
the prompt file by hand. The move sessions'
log is `~/Documents/Development/cabinetos-audit/moves-log.md`. Each later move
takes its prompt from Part 6 section 28 and its batch file the same way.

**How every move session ends** (Part 6 section 27): the final commit pushed
to testing as a plain fast-forward, the image built and proved from the A9,
the self-tests passed, the journal read (the sound line, the launch and save
lines the batch names, the `[pace]` line after any frame-loop move, a normal
exit after Add an account after any accounts move), the captures compared,
the pull request body with "Changelog: none" and the faults left as they
were; then MMagTech judges what the batch's TV list names and merges into
`restructure`. One exception (MMagTech, 2026-10-14, 11:20 PM): batch (a),
whose TV list is empty, merges into the branch by itself when every machine
check passed. Never two batches on one branch, never a fix inside a move,
and the final merge to main is his.

**Decided 2026-10-14 (11:14 PM): the merge strategy.** Every batch is a pull
request into the long-lived `restructure` branch (on GitHub, cut from main at
77d2188); `latest` stays at 2026.10.14.5 until one final pull request to
main, judged with the full checklist on its testing image; then the branch
is deleted and testing and latest work as before.

**Decisions MMagTech owes before move 1 merges** (Part 6 section 29, each
with a recommendation): which of Part 3's 50 faults get issues now (recommended: eleven,
named there); protectRomId 0 (recommended: unchanged through the moves,
decided after batch (b)); folders for the files (recommended: folders, with
unique basenames); the test instruments (recommended: in the product, in
their own files, revisited after batch (g)); the names (keyboardResult and
the types declared inside main()). The backup step before move 1 is done in part: the tag 2026.10.14.5 is on
72c0c4b and pushed; the revert commands (Part 6 section 26) go into
docs/WORKING.md with move 1's pull request.

**Settled and not to be reopened:** the client's thread rule is fixed in
batch (i), after Part 5 (MMagTech, 2026-10-14); the two PIN gates (decided
2026-10-07, in the code at main.cpp 9501 to 9509); #402 corrected; #404, #405
and #406 opened, fixed as their files move or sooner on his call.

**Where the material is:** `~/Documents/Development/cabinetos-audit/` on
the Mac (outside the repository): `readers/` (twenty-eight reader outputs),
`verifiers/` (twenty-five refuter outputs and the nine batch files),
`verify-args/`, `measurements/`, `scripts/` (the briefs and the batch
definitions in `part6-batches.md`), the posted comments as
`part1-comment*.md` to `part6-comment*.md`, and the run logs `part3-log.md`
and `part6-log.md`. A new session starts from these, not from re-reading.

## Waiting, not for the next session unless MMagTech raises them

- #401, "RomM 5.4.0: a game's page cannot read its time played without
  devices.read or a device_id" and #402, "A one-file game whose file name
  begins with # or ? fails to download (the name is cut before the server
  sees it)": found by Part 1, report only; #402 corrected by Part 2.
- #404, "The launch worker writes the job's entry path while the frame
  thread reads it"; #405, "favorites.json is written from two threads through
  the same temporary file with no lock"; #406, "Four background threads are
  never stopped at exit (pairing, remove and sign out, reconnect, drive
  backfill)": found by Part 2's refutation, report only; each is fixed as
  its feature's file leaves main.cpp, or sooner on MMagTech's call.
- #329, "Release notes for every update, and a way to see them from the
  console"; #380, "The console follows any web address in RomM's data, with
  the RomM sign-in attached"; #381, "Remote Play: stream with no screen
  connected"; the Bazzite base update #371.
- #387, "A cover fixed on RomM shows only after the console restarts" (also
  deleted games; must not conflict with offline or the caches).
- #390, "Headphones don't take over after a Remote Play session (waiting on
  Sunshine)": MMagTech writes the Sunshine report in his own words.
- #391, "Headphones in at start get full volume": left as is, revisit with
  Bluetooth headphones.
- #393, "Power off and Restart cut the picture abruptly": a fade only; no
  sound, ever.
- #181, "Updates and DLC as separate files in RomM (Switch, PS3, Xbox 360)":
  always applied; a "game updated" notice only on plays after the first.
- deletableman (#341, #376): nothing new from him. A reply for #341 was
  drafted and not posted; it is MMagTech's call.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
