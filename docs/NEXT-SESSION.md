# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-17, early morning:** `latest` is **2026.10.14.5** (built from
72c0c4b, tagged in git). The A9 (Wi-Fi, **192.168.1.109**; Tailscale
cabinetos, 100.67.82.30) is on the testing channel and runs **2026.10.17.3**,
the restructure's final image, installed through its own Settings > System
update; its PIN is OFF until MMagTech's checklist turns it back on. RomM's
documentation together with RomM's behaviour is the source of truth for
anything on RomM's data.

**All the restructure's moves are merged into `restructure`** (#398, "Audit
the code and restructure it for contributors (main.cpp is 20,000 lines)"),
unattended since 2026-10-16, 8:48 PM on MMagTech's word: move 6, batch (h1),
#418 (testing 2026.10.17); move 7, batch (h2), #419 (2026.10.17.2); move 8,
batch (i), #420 (2026.10.17.3), which also fixes the RomM client's thread
rule. Each batch had the machine checks and its own testing image, installed
on the A9 and proved from its log; none had a TV round of its own.

**What is left is MMagTech's:** the one checklist on the final image, in the
pull request that takes `restructure` to main (its body holds the checklist
with boxes to tick), then his "merge", which promotes 2026.10.17.3 to
`latest`, the only step that reaches players; then his go to delete the
merged move branches (listed in that pull request).

## Next

- If the checklist finds a fault: narrow it down by switching the A9 to each
  batch's testing image (testing-61e1e84 for (h1), testing-f01296c for (h2),
  testing-f4aa82c for (i)); fix it in its own pull request into
  `restructure`, push to testing, install, and run the failed step again.
- After the merge to main: Parts 4 and 5 of the audit (report only; quote the
  cost before any agent wave), then the contributor guide, then the faults
  the moves left as they were (each batch's pull request lists them), each in
  its own pull request; then features and bug fixes resume.
- The record of every move, its checks and its scripts is in
  `~/Documents/Development/cabinetos-audit/moves-log.md` and `scripts/move1/`
  to `scripts/move7/`.

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
