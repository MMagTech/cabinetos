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

**2026-10-20, early.** `latest` is **2026.10.19.3** (#519). `testing` is
**2026.10.20** (revision b1574c0), the night batch: PR #PRNUM, "PRTITLE",
waiting on his TV check and merge. Eight sessions built it on the evening
of 2026-10-19 on separate files; one combined it, built it in the builder,
ran every self-test and smoke-tested it headless on the A9 through the
read-only relay. It closes #305 and #492 and finishes six items of #484.
**19 issues are open**, 17 once the PR merges. A RomM server for write
tests now runs on the test VM (RomM 5.4.0, its own admin login, nine free
homebrew games): the tools read it from `CABINETOS_TEST_ROMM` in his local
notes, and it is the only RomM server a test may write to; the live one
stays behind the read-only relay. His server is on RomM 5.4.0. The A9 is
on the testing channel; its PIN is off. The machines' addresses are in his
local notes, which CLAUDE.md loads; the tools read them from the
environment (export the `CABINETOS_*` lines per command; the Mac's shell
profile does not set them).

**His rules from 2026-10-19:** an unattended run never waits on a prompt
(every session's brief says to skip what needs approval and carry on, and
the parent sets a timed fallback); decisions are put to him one at a time,
mechanics first, never with options he did not ask for; a feature with
hidden state is shown to him on the TV, never explained again in prose
(#313 below); the console keeps its narrow RomM token and never asks for
`me.write` (rommapp/romm#5274 asks for a self-revoke route); an opt-in,
disclosed upload to a site under his control is his exception to "only
RomM and GitHub" (#215). No issue is opened unless he asks.

## Next

1. **His TV check of 2026.10.20, then the merge on his go** (the list is in
   PR #PRNUM, "For the TV"). After the merge: delete the batch branches
   (`claude/ovn5-*` except `claude/ovn5-stream-3d2bf4` and
   `claude/ovn5-484-romm10`, `claude/test-romm-*`, `claude/checks-week-*`,
   `claude/cabinet-docs-*`, `claude/research-3ds-*`, `ovn5-2026-10-19`) and
   their worktrees.
2. **#313 "On a stream, Select can open the menu instead of reaching the
   game": shown, not explained.** The Home-always build is on
   `claude/ovn5-stream-3d2bf4` (dcd2728: a controller through a stream
   always uses Home as its menu button; TV pads keep their saved choice).
   Deploy it to the TV with `tools/ui-loop.sh` while he streams with
   Moonlight in his hand: he taps Select in an arcade game, holds Select,
   presses Home on a Bluetooth controller. Then build and deploy his own
   rule beside it: the saved button follows a controller onto a stream,
   except Select, which becomes Home there (the console cannot tell an
   Xbox-type controller on the client from Moonlight's on-screen buttons,
   so Select must stay with the game). He decides at the TV. Six written
   explanations failed on 2026-10-19; do not write a seventh.
3. **Words from him that close or settle issues, no work:** #253 (the
   night's hour of measurements found nothing to change: the misses the
   issue was filed for are gone on gamescope 3.16.31, what is left is one
   held picture at Home about 0.4 a minute; close with "reopen on a
   report"); the 3DS (PROJECT.md question 50: go with conditions, a hand
   test on the A9 at a time he names, no 3DS games on his server today;
   he says whether there is an issue); #484's "a drive unplugged during
   Sign out" (the docs now say what happens; what to do is his call);
   #224, #181, #215 (designs on their issues).
4. **The automatic checks turn strict on 2026-10-26** and `main` passes
   all four; nothing to do but watch the first pull request after.
5. **The monthly health check.** With the next real core change: #490's
   comments in the core and PCSX2 build files.

## Waiting, not for the next session unless MMagTech raises them

- **Needs the TV or a device:** #473 (a press while an emulator of its own
  has the screen), #411 (Moonlight), #381 (the HDMI cable out), #290 (the
  C1, before a release), #228 (the measurement in PROJECT.md section 46
  when the sound dies again), #132 (the Bluetooth wake test).
- **Waiting on others:** #297 and #390 (Sunshine), #346, #353 and #362
  (RomM's save channels; revisit mid-January 2027), ROMM-10 on #484
  (rommapp/romm#5274; the revoke code is on `claude/ovn5-484-romm10`),
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
