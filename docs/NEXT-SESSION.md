# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-17:** `latest` is **2026.10.17.3** (revision f4aa82c, digest
sha256:f84aa879...dfa8), promoted at 8:48 AM by the merge of #421
("The restructure: main.cpp's 20,000 lines and the other big files split into
files named for what they do") into main as 27abe89. The A9 (Wi-Fi,
**192.168.1.109**; Tailscale cabinetos, 100.67.82.30) is on the testing
channel, runs 2026.10.17.3, and its PIN is on. testing and latest hold the
same image and work as they did before the restructure: a branch cut from
main goes to testing as a plain fast-forward. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

**The restructure of #398 ("Audit the code and restructure it for
contributors (main.cpp is 20,000 lines)") is done and shipped:** moves 1 to
8 (#409, #410, #413, #414, #415, #418, #419, #420), MMagTech's checklist on
the final image (all 22 TV steps good), and the move branches deleted. Each
batch's image keeps its tag for narrowing a fault down on the A9:
testing-61e1e84 (h1), testing-f01296c (h2), testing-f4aa82c (i); the console
before the restructure is 2026.10.14.5 (git tag on 72c0c4b).

## Next

Part 5 of the audit (report only; change no code): the newcomer docs
measured against the code, and the refutation of the RomM map. Its prompt is
`~/Documents/Development/cabinetos-audit/part5-prompt.md`. Four of its agents
already ran in the Part 4 session and are saved in that folder (the docs
reader, every stale reference, the two RomM map checks); never rerun them.
Quote the cost before any agent and wait for MMagTech's go. Then the
contributor guide, then the faults the moves left as they were and those
Part 4 recorded (#398, Part 4 section 38), each in its own pull request; then
features and bug fixes resume.

Part 4 is posted on #398 (three comments, 2026-10-17): the scripts, services,
image build, cores, CI, workflows and tools. Its record is `part4-log.md`,
`readers/area-6-system.json` to `area-10-tools.json` and the same names in
`verifiers/`.

**Owed by MMagTech:** Part 4 section 40 (which faults get issues, whether the
flatpak retry waits, which splits happen, a workflow to re-point latest, his
post-refactor cleanup plan); Part 6 section 29 decisions 2 to 6; #408 ("Next
session: Part 6 of the #398 audit is posted; the moves begin with batch (a)")
is still open against main and out of date.

The record of every move, its checks and its scripts is in
`~/Documents/Development/cabinetos-audit/moves-log.md` and `scripts/move1/`
to `scripts/move7/`; what the moves taught is in docs/lessons/testing.md.

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
- #422, "Primal Rage (PS1) will not start: PCSX-ReARMed says its CHD is not
  a valid CD image": found in the checklist; the file matches RomM's.
- deletableman (#341, #376): nothing new from him.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
