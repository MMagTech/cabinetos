# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-14:** `latest` is **2026.10.14.5**. The A9 (Wi-Fi,
**192.168.1.109**) runs it. RomM's documentation together with RomM's
behaviour is the source of truth for anything on RomM's data. RomM was asked
to send its `version` tag (rommapp/romm#5243); nothing to do until it does.

**Features and bug fixes are paused for a restructure (MMagTech,
2026-10-14).** Performance is not the reason: features must stop breaking
each other, and outside contributors must find their way.

## Next

**The audit (#398, "Audit the code and restructure it for contributors
(main.cpp is 20,000 lines)") is being done in parts, one per session, report
only, no code changes.** Parts 1, 2 and 3 are posted on the issue (2026-10-14):
every line of main.cpp is read, its plan is 61 files (Part 2, section 3.12),
the whole plan is refuted and corrected (Part 2 section 14, Part 3 section 19),
the dead-code list is settled (section 15), and every other frontend file is
read and refuted in five groups (Part 3, sections 20 and 21). **Part 4 is
next**; its prompt is `~/Documents/Development/cabinetos-audit/part4-prompt.md`:
the scripts, services, image build, cores, CI, workflows and tools, in Part 1's
remaining areas, then their refutation. Part 5 is the docs and the RomM map's
refutation; Part 6 the final order of moves and the checklist.

**Decisions MMagTech owes before the moves begin** (Part 2 section 17, Part 3
section 23): the pace of the moves (one area per session as decided, or the
batched plan of Part 3 section 22.5; he is thinking, not deciding), backup and
revert before move 1, which of Part 3's 25 new faults get issues, protectRomId
0, folders for the 61 files, whether the test instruments stay in the product
binary, and the dead-code pull request. Decided 2026-10-14: the client's thread
rule waits for the RomM folder move; the two PIN gates were decided 2026-10-07
(in the code at main.cpp 9501 to 9509); #402 corrected; #404, #405 and #406
opened.

**How a part runs (learned 2026-10-14):** three to five agents at a time,
every output saved into `~/Documents/Development/cabinetos-audit/` as it
lands, the part posted on #398 before the session ends. Readers on Opus (Fable
only for files where the lessons cluster), refuters on Opus, mechanical checks
on Sonnet with a spot-check; the cost of each wave read off the usage card
first; up to ten agents at once when each writes its own file. Give every time
in MMagTech's clock (US Eastern), never UTC.

**Where the material is:** `~/Documents/Development/cabinetos-audit/` on
the Mac (outside the repository): `readers/` (twenty-eight reader outputs),
`verifiers/` (twenty-five refuter outputs), `verify-args/` (the posted sections,
the raw claims, the dead-code batches and the grep baseline),
`measurements/`, `scripts/`, and the posted comments as `part1-comment*.md`,
`part2-comment*.md` and `part3-comment*.md`; `part3-log.md` is the run log. A new session starts from these, not from
re-reading.

**Then, after MMagTech's decisions:** one area per session, moved, built and
run on the A9, the checklist repeated, a testing image, a pull request he
merges. Never two areas at once. The contributor guide comes last.

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
