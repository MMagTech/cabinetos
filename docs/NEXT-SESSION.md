# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `CONTRIBUTING.md`, then `docs/ARCHITECTURE.md`.
The specification is `docs/PROJECT.md`; its index, at the top, gives
every section and every numbered question with where it ended. Name every issue and pull request by
its title as well as its number, every time. Replies to reporters are in
MMagTech's voice ("I"), ask them to test, and never claim a fix nobody
reproduced. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

## Where things stand

**2026-10-19, morning.** `latest` is **2026.10.18.7** (#516, "Ready for RomM
5.4, and six small fixes a player can meet", merged and promoted on his go
2026-10-18). `testing` is **2026.10.19.2** (revision a760620): #517, "Overnight
batch", built unattended overnight by nine sessions on separate files and
combined on branch `overnight-2026-10-19`. Not merged. The A9 is on the
testing channel and still runs 2026.10.18.7; its PIN is off. His server can
move to RomM 5.4 now that #516 is out (database backup first; #401 was not
run against a 5.4 server). The machines' addresses are in his local notes,
which CLAUDE.md loads; the tools read them from the environment
(`CABINETOS_A9`, `CABINETOS_KEY`, `CABINETOS_VM`, `CABINETOS_RELAY_UP`).

**His rules from 2026-10-18:** no issue is opened unless he asks for one
(findings go in the pull request or PROJECT.md); FSR for Xbox 360 stays off
(PROJECT.md, question 34). His decisions that night, with the items still
open, are in #517's body.

## Next

1. **Finish #517.** He installs 2026.10.19.2 on the A9 (Settings, System, System
   update), then the TV check, one step at a time, waiting for "go":
   - A Genesis game (Sonic) fills a 4:3 shape, not stretched wide (#220).
   - Open a system's grid: covers lift a little slower, 180 ms (HOME-06).
   - Destroy All Humans! 2's page says "Disc can't be read" (#506).
   - Start a game, open the Power menu, choose Restart: the picture fades
     to black over about a second, silently, then restarts (#393).
   - Super Mario Land signed in to RetroAchievements: save a state, play
     on, load it; the log says "[ra] the state's achievement progress
     restored" (#279).
   - A PS3 game plays; RPCS3's log says "VSync Mode: Full" (read it, no TV).
   - An N64 game plays without a hitch twice a second (#254).
   - The usual: a game with sound.
   Then merge on his go. It closes 22 issues (listed in #517); two commits
   say "Fixes #473" and "Fixes #222" though both are only partly done, so
   GitHub closes them too: reopen both at once with what is left (#473: the
   power button while an emulator of its own has the screen; #222: the
   retry fallback). Then strike from #485's list the "Add an account code
   approved while closed" item (his call: works as is). #422 closes as not
   a console fault: his server's PS1 Primal Rage file is the Saturn disc.
   **#511, measured:** the 254 MB splash layer came out identical between
   the two overnight images (19308fe, a760620), where it used to change
   every time. The 70 MB OS layer changed, as it must: two scripts in
   system_files/ changed between them. Its proof is the next image built
   with no change under build_files/ or system_files/ (method in #517).
2. **The priority list** of the 37 still open, as one short list for his
   yes or no (drafted in the 2026-10-19 morning message): fix next, needs
   the TV, waiting on others, needs his decision, close.
3. **The automatic checks report only until 2026-10-25**, then fail a pull
   request: read their first week's reports and fix what they find
   (CONTRIBUTING.md, "What CI checks").
4. **Review the docs for Cabinet Go**: Cabinet is now the pocket version
   for handhelds, unfinished; the docs that call it "the Apple TV app"
   (CONTRIBUTING.md, docs/CABINET.md, PROJECT.md's banner, README.md, the
   site) need reviewing.
5. **The monthly health check** starts after item 3.

## Waiting, not for the next session unless MMagTech raises them

- **His decisions:** #391 (headphones at full volume), #291 (the Rumble
  row's place), #484's original-Xbox item (a second person blocked by
  another's stranded save: he wants another way; the Xbox 360 way, saves
  copied off to their owner at the next start, was offered), #288 and #339
  (set aside; reasons in coreoptions.cpp and PROJECT.md question 34).
- **The game page's launch messages, later** (MMagTech, 2026-10-19, at the
  TV): "Disc can't be read" sits as a loose grey line under Download, as do
  all ten or so reasons Play or Download fails ("Couldn't start this game",
  "Not enough space", "No PS2 BIOS on your server", the keys and licence
  ones). He wants their placement (the Play row's right end, in amber, was
  offered; or the bottom pill) and their wording reviewed together, with a
  way to cycle through each on the TV first, like `--notice-gallery`: he
  has never seen half of them. Not an issue.
- **Waiting on others:** #297 and #390 (Sunshine), #346 and #353 (RomM's
  save channels; revisit around mid-January 2027), #487's
  BASE_UPDATE_TOKEN secret (his), Xenia Edge taking Canary's Unreal tiling
  fix (PROJECT.md, question 34).
- **#228, sound dying:** investigated 2026-10-19 (PROJECT.md section 46, a
  leading cause and the measurement that would confirm it, on the TV).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
