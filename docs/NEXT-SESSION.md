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
only, no code changes.** Part 1 is posted on the issue (2026-10-14): the
numbers, main.cpp lines 1 to 9147 with a split proposal, the RomM map with
what 5.4.0 changes, the dead-code and duplicate candidates, "where to
worry", the "same as before" checklist, a first order of moves, and the
phasing of Parts 2 to 6.

**Why parts (learned 2026-10-14):** one run over the whole OS was cut by
the session's usage limit with 18 readers mid-read, and their work was lost.
A session now runs three to five readers at a time, verifies what they
found, posts its part on #398 before it ends, and stops.

**MMagTech decides the size before Part 2 starts:** Part 2 only (main.cpp
9148 to 20018 and verification of the main.cpp plan, then the moves begin;
the other areas get read one file at a time as they are moved); or Parts 2
and 3 (all of the frontend); or Parts 2 to 6 as listed in Part 1's third
comment.

**Where the material is:** `~/Documents/Development/cabinetos-audit/` on
the Mac (outside the repository): `readers/` (the twelve finished reader
outputs as JSON, and the lessons-to-code fragility map), `measurements/`
(the main.cpp block and lambda map, the shared-state measurement, the A9
linker list of unreferenced functions, the RomM 5.3.1 OpenAPI spec),
`scripts/` (the scan scripts, the survey workflow, the verification
workflow and its argument builder), and the three posted comments. A
new session starts from these, not from re-reading.

**Then, after MMagTech's decision:** one area per session, moved, built and
run on the A9, the checklist repeated, a testing image, a pull request he
merges. Never two areas at once. The contributor guide comes last.

## Waiting, not for the next session unless MMagTech raises them

- #401, "RomM 5.4.0: a game's page cannot read its time played without
  devices.read or a device_id" and #402, "A game whose file name contains
  #, & or ? is cut on the one-file download": found by the audit, report
  only; his decisions are in Part 1's section 13.
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
