# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time. Replies to reporters are in MMagTech's voice ("I"),
ask them to test, and never claim a fix nobody reproduced.

## Where things stand

**2026-10-14:** everything from the last session is merged and released:
covers in every format (#376), the grid heading, the sound rule (#341,
PROJECT.md question 46), and interface sounds everywhere with a calmer set
(#389, question 47), `latest` 2026.10.14.4 once merged. The A9 (Wi-Fi, **192.168.1.109**) runs the newest
`testing`. RomM's documentation together with RomM's behaviour is the source
of truth for anything on RomM's data; nothing beyond it is invented.

## Next

1. **A design discussion, not a build: same-name games, regions and
   versions** (#383, "Game versions, updates and DLC: one place to choose
   them", which also parks the earlier thinking). MMagTech, 2026-10-14: settle
   the design choices first, then build, in the session after or later in the
   same one if there is room.
   - **Facts first, read-only:** what RomM's documentation says it records
     (regions, languages, revisions, tags) and what RomM actually returns for
     MMagTech's own same-name games, as a short list with real examples from
     his library. RomM's docs and behaviour are the line; nothing beyond them.
   - **Then an open discussion, not yes/no lists:** he wants room to give his
     opinions. Lay out the real options with my view and their downsides
     (where versions show: the heading, the game's page, the long press on
     Play kept for rare options; which version Play picks; updates and DLC),
     then let him talk it through.
   - **The outcome:** the decisions written onto #383 and into PROJECT.md,
     ready to build.

## Waiting, not for the next session unless MMagTech raises them

- #329, "Release notes for every update, and a way to see them from the
  console" (his Discord group's answer about a QR code).
- #380, "The console follows any web address in RomM's data, with the RomM
  sign-in attached"; #381, "Remote Play: stream with no screen connected";
  the Bazzite base update #371.
- #387, "A cover fixed on RomM shows only after the console restarts" (also
  deleted games; must not conflict with offline or the caches).
- #390, "Headphones don't take over after a Remote Play session (waiting on
  Sunshine)": MMagTech writes the Sunshine report in his own words (their
  rules ban AI-written issues); the facts are on the issue.
- #391, "Headphones in at start get full volume": left as is, revisit with
  Bluetooth headphones.
- #393, "Power off and Restart cut the picture abruptly": a fade only; no
  sound, ever.
- deletableman (#341, #376): nothing new from him. A reply for #341 was
  drafted and not posted; it is MMagTech's call.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
