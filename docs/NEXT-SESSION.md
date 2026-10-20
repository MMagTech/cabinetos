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

**2026-10-20, morning.** `latest` is **2026.10.20** (#521, merged the evening
before). `testing` is **2026.10.20.2** (revision 6d05cb3): PR #525, "Select
is never the shortcut button, Home always opens the menu (#313); Search's
covers follow the keyboard", judged on the TV through the loop on the
morning of 2026-10-20, waiting on his check of the image and his merge.
It closes #313: his decision after a long morning was the plainest rule,
Settings refuses Select and Start as the shortcut button and a chosen
button is added beside Home, never in its place; the two builds that gave a
stream its own rule were set aside unjudged. Overnight, on his go, four
Opus sessions ran: the Vectrex stall measured on the TV (no stall in six
runs at 1x or 4x; `claude/vectrex-pacing`, not pushed), the Vectrex sheets
built for #224 (judged good on the TV; its session is finishing the fit
check and the pull request), the 3DS core built in CI (draft PR #524), and
the Vita pointer research (draft PR #523). PR #522 is the Vectrex sheets'
research, docs only. The A9 is on the testing channel; its PIN is off. The
machines' addresses are in his local notes.

**His rules from 2026-10-19 and 2026-10-20:** anything mechanical (a
measurement, a build, research) runs in an Opus session, never in the Fable
parent; an unattended run never waits on a prompt; decisions one at a time,
mechanics first, in a player's words, never with options he did not ask
for; a feature with hidden state is shown on the TV; no issue is opened
unless he asks. Every loop launch past a minute records time played on his
RomM: keep test launches under a minute, or have his go.

## Next

1. **His check of 2026.10.20.2, then the merge of PR #525 on his go.** On
   the phone: tap Select in an arcade game puts a coin in, hold Select opens
   the menu. Settings, Controllers, Shortcut button, A, Select: "Games use
   Select…". Search: the covers grow with the keyboard's slide. After the
   merge, delete `tv-2026-10-20`, `claude/313-select-refused` and
   `claude/search-keyboard-slide` and their worktrees; the two set-aside
   #313 branches (`claude/ovn5-stream-3d2bf4`, `claude/ovn5-stream-select-home`)
   and the merged `ovn5-2026-10-19` go when he says so (asked 2026-10-20).
2. **#224, the Vectrex sheets:** its session's pull request, then its own
   testing image and his check (the Overlays row, Mine Storm, Armor Attack).
3. **The 3DS, draft PR #524:** waits for a dumped game of his and a time
   for the A9 hand test (PROJECT.md question 50, "The A9 hand test"), and
   his acceptance of decrypted dumps only.
4. **Words from him, one at a time, no work:** #253 (close with the drafted
   comment: the misses are gone on gamescope 3.16.31; "reopen on a
   report"); #484's "a drive unplugged during Sign out"; the Vita's upstream
   ask (support on Vita3K/Vita3K#3707, PR #523's recommendation); #181 and
   #215 (he is unsure of both).
5. **The Vectrex stall:** no fix; the next step is his evening journal of
   2026-10-19 read for what ran just before each stall (PROJECT.md, "The
   4K misses measured again", the Vectrex record). Only if it comes back.
6. **The automatic checks turn strict on 2026-10-26**; nothing to do but
   watch the first pull request after.
7. **The monthly health check.** With the next real core change: #490's
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
