# Next session

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `CONTRIBUTING.md`, then `docs/ARCHITECTURE.md`.
`docs/PROJECT.md` is the record of what was decided and why: look up the
question in hand (its index, at the top, gives every section and numbered
question with where it ended), never read it through. Name every issue and pull request by
its title as well as its number, every time. Replies to reporters are in
MMagTech's voice ("I"), ask them to test, and never claim a fix nobody
reproduced. RomM's documentation together with RomM's behaviour is the
source of truth for anything on RomM's data.

## Where things stand

**2026-10-20, night.** The 3DS is in: PR #524, "Nintendo 3DS through
Azahar's own libretro core (decrypted dumps)", merged on his go, and every
console is offered 2026.10.21.2. Its last fix was not the 3DS: moving up
from the Library's fourth system and then opening any system crashed the
console's program (a write past a two-entry array, since c1fa0bb), found
on the TV that night and fixed in the same PR. The Azahar patches each have
a tracking issue linked to a request upstream: #534, "Azahar patch: each
game sets its own folders (until Azahar fixes SetUserPath)"
(azahar-emu/azahar#2694), and #537, "Azahar patch: games get no network
(until Azahar has an option)" (azahar-emu/azahar#2695). Also merged: #526
(the Vectrex sheets, #224), #535, "PS Vita: the settled build plan, in
question 49", and #536, "PROJECT.md's header: a record to look things up
in, not read first". The A9 is on the testing channel and still runs
2026.10.21; its PIN is off. The machines' addresses are in his local notes.
His standing rule: every check and every comment is written out in full
each time it is asked for, never referred to.

**His rules from 2026-10-19 and 2026-10-20:** anything mechanical (a
measurement, a build, research) runs in an Opus session, never in the Fable
parent; an unattended run never waits on a prompt; decisions one at a time,
mechanics first, in a player's words, never with options he did not ask
for; a feature with hidden state is shown on the TV; no issue is opened
unless he asks. Every loop launch past a minute records time played on his
RomM: keep test launches under a minute, or have his go.

## Next

1. **The PS Vita (Vita3K), now.** Build it to PROJECT.md question 49,
   "Decided, 2026-10-20: the build plan": every choice there is settled,
   build it as written. The probe on the A9 comes first (a touch from the
   console's own virtual touch screen reaching Vita3K as a finger); if it
   fails, stop and bring him the patch route, named, before any code. Then
   the build, and before asking for a testing image, what a person would
   hit: two games in a row, two people (switch person, then browse and
   open the Vita shelf), the save round trip, offline, every quit path.
   The testing push only on his words; the hand test from the plan's list
   with every step and its success written out first; the merge on his
   word "merge". The Vita does not touch `cores/build-core.sh` (its
   AppImage installs as Xenia's does), so it does not wait on item 2.
2. **PR #533, "CI: a change to one emulator rebuilds only that
   emulator":** update it from main (four conflicts, all in comments, in
   `cores/build-core.sh` and `cores/arcade-sets.sh`), check the azahar
   arm and both its patches are in its cache key, and write "#537" beside
   the no-network patch's comment in the same change (any edit to
   `build-core.sh` before #533 rebuilds all 23 cores). A testing image to
   prove the caching, then the merge on his word. Its session is "Rebuild
   only the core whose recipe changed".
3. **Stop at an out-of-range array write, at the line that does it.**
   Tonight's crash was a write past a two-entry array that landed
   silently on the grid screen next to it. Build the frontend with
   `-D_GLIBCXX_ASSERTIONS` and make the fixed arrays indexed by a value
   set at run time `std::array` (`pillFocus_` in `libraryscreen.h`,
   `slotFocus` and `slotPress` in `app.h`, and the others a grep finds),
   so a bad index stops the program and names the line instead of
   damaging what sits beside it. Measure the frame times on Home and a
   large grid on the A9 before and after; if the cost shows, stop and
   say so. One PR, CI green, his word to merge.
4. **Kirby's first save, inside Mario Kart 7's.** Before the folder fix,
   Kirby: Planet Robobot saved into Mario Kart 7's folder, so Mario Kart
   7's save on his RomM still carries Kirby's first save (4 files). He
   wants it gone (2026-10-20). Each Mario Kart 7 start unpacks RomM's copy
   over the folder, so deleting it on the A9 alone does not last: Mario
   Kart 7's save on RomM has to be replaced. Restate exactly what goes and
   wait for his yes before anything is deleted.
5. **#529's leftovers, his word first:** PROJECT.md question 32 still says
   a controller connected mid-game waits for the next launch, without
   eden-emulator/Issue-Reports#739; a stale Batocera comment in
   `frontend/src/standalone_eden.cpp` near line 327.
6. **Words from him, one at a time, no work:** #181 and #215 (he is
   unsure of both).
7. **The Vectrex stall:** no fix; the next step is his evening journal of
   2026-10-19 read for what ran just before each stall (PROJECT.md, the
   Vectrex record under "The 4K misses measured again"). Only if it comes
   back.
8. **The automatic checks turn strict on 2026-10-26**; nothing to do but
   watch the first pull request after.
9. **The monthly health check.** With the next real core change: #490's
   comments in the core and PCSX2 build files.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #473 (a press while an emulator of its own
  has the screen), #411 (Moonlight), #290 (the
  C1, before a release), and #228 (the measurement in PROJECT.md section
  46 when the sound dies again).
- **Waiting on others:** #297 and #390 (Sunshine), #346, #353 and #362
  (RomM's save channels; revisit mid-January 2027), #545, the RomM
  token revoke (rommapp/romm#5274; the code is PR #528),
  #203 closed until libretro/dolphin#508 moves.
- **An idea of his, 2026-10-19, not scheduled:** a channel-change animation
  when a game starts and when it exits, on the console's own layers only,
  silent, no setting; pause stays instant.
- **The game page's launch messages, later** (MMagTech, 2026-10-19, at the
  TV): "Disc can't be read" sits as a loose grey line under Download, as do
  all ten or so reasons Play or Download fails. He wants their placement
  (the Play row's right end, in amber, was offered; or the bottom pill) and
  their wording reviewed together, with a way to cycle through each on the
  TV first, like `--notice-gallery`. Not an issue.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
