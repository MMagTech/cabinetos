# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `CONTRIBUTING.md`, then `docs/ARCHITECTURE.md`.
The specification is `docs/PROJECT.md`. Name every issue and pull request by
its title as well as its number, every time. Replies to reporters are in
MMagTech's voice ("I"), ask them to test, and never claim a fix nobody
reproduced. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

## Where things stand

**2026-10-17:** `latest` is **2026.10.17.3** (revision f4aa82c, digest
sha256:f84aa879...dfa8), promoted at 8:48 AM by the merge of #421
("The restructure: main.cpp's 20,000 lines and the other big files split into
files named for what they do") into main as 27abe89. The A9 is on the testing
channel, runs 2026.10.17.3, and its PIN is on; its address and the other
machines' are in MMagTech's local notes, which CLAUDE.md loads (they left the
repository on 2026-10-17). testing and latest hold the same image: a branch
cut from main goes to testing as a plain fast-forward.

**The contributor guide is written** (#398, "Audit the code and restructure
it for contributors (main.cpp is 20,000 lines)"), step 1 of the plan
MMagTech approved on 2026-10-17 at 7:04 PM (recorded on #398):
`CONTRIBUTING.md`, `docs/ARCHITECTURE.md` (the map of the code first),
`CLAUDE.md` (his six rules, for this code) and `CLAUDE.local.example.md`.
Two Opus agents read it cold, one after the other, and their findings were
fixed (step 2). Its record is `guide-log.md`, `verifiers/guide-coldread.json`
and `verifiers/guide-coldread-2.json` in the audit folder (the folder's path
is in the local notes).

## Next

Two sessions at once, both starting as soon as MMagTech merges the guide
(MMagTech, 2026-10-17, about 8:20 PM: "go" to running them in parallel):

1. **The docs tidy** (step 3; docs only, "Changelog: none", aimed at a few
   hours): the PROJECT.md index, the three history files into
   docs/history/ with their citations fixed, frontend/README.md cut down to
   the build, the stale lines docs/ARCHITECTURE.md section 7 lists, a pull
   request template; the machine details still in the other docs only on
   MMagTech's yes, asked once at the start. Prompt: `docs-tidy-prompt.md`
   in the audit folder.
2. **The faults and the automatic checks** (step 4): first one list of
   every recorded fault (#398, Parts 3 to 5, and what the moves left);
   fixed now only what a player can meet and the checks that do not check
   (about 8 to 12 pull requests), the rest filed as issues grouped by area
   (about 15 to 20), no question per fault; MMagTech answers the one list.
   Fixes are coded in parallel sessions, go into a `faults` branch and are
   tested in three or four batches, with one checklist for him at the TV
   for the few that change the screen. It edits no doc until the docs tidy
   has merged. Prompt: `faults-prompt.md` in the audit folder.
3. Then features and bug fixes resume. The monthly health check (step 5)
   starts after that.

**Owed by MMagTech**, settled inside the faults step's one recommended list,
not asked one by one: Part 5 section 54; the wording of #401 and #402 (Part 5
section 51); Part 4 section 40; Part 6 section 29 decisions 2 to 6. #408
("Next session: Part 6 of the #398 audit is posted; the moves begin with
batch (a)") is still open against main and out of date.

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
