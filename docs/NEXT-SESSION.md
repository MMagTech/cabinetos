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

1. **Build the same-name heading** (#383, "Same-name games: show RomM's
   tags in the heading"), decided 2026-10-14, PROJECT.md question 48.
   Every game keeps its own tile. When two games on one system share RomM's
   `name`, the grid heading and Home's shelf header show RomM's `regions`,
   `languages`, `revision` and `tags` for the selected game, dimmer, after
   its name; nothing when RomM sends none. No grouping, no picker, nothing
   written to the server. Lessons: `frontend.md`. Judge it on the TV in the
   loop with the Jaguar alternates, Pokémon Emerald,
   Metroid Prime Hunters and Urban Reign; a long tag line must fit the
   heading. **With it, in the same pull request, the diagnostic report gets a
   "Same-name games" list** (MMagTech, 2026-10-14: a player asked for this
   and he cannot test their library): per system, each shared name, and per
   copy the file name, RomM's fields as received and the heading shown.
   Built only when a report is made, playable systems only. Check it in a report from the A9.
   Then testing image, changelog entry, pull request.

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
