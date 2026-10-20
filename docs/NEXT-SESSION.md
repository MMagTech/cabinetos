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

**2026-10-20, morning.** PR #525, "Select is never the shortcut button,
Home always opens the menu (#313); Search's covers follow the keyboard",
merged on his go after its first image (2026.10.20.2) was judged on the TV
through the loop and the second carried only wording; it closes #313. His
decision after a long morning was the plainest rule: the row reads
"Shortcuts use: Home", refuses Select, Start, L3 and R3 with the reason,
and a chosen spare button is added beside Home, never in its place; the two
builds that gave a stream its own rule were set aside unjudged and deleted.
His standing rule from that morning: every check and every comment is
written out in full each time it is asked for, never referred to. Overnight, on his go, four
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

1. **The 3DS, on the A9, now.** MMagTech has 3DS games on his server as of
   2026-10-20. Draft PR #524, "Nintendo 3DS through Azahar's own libretro
   core (decrypted dumps)", is built and green on
   `claude/3ds-azahar-core-9a119a`; PROJECT.md question 50 holds the
   design and "The A9 hand test, when he names a time". Brief controller
   questions first (the right stick and R3 for touch, the DualSense
   touchpad, the two screens' layout), one at a time, then the hand test
   through the loop, then its own testing image. Mechanical work in Opus
   sessions; this parent reads, decides and reports.
2. **#224, the Vectrex sheets:** PR #526, judged good on the TV
   2026-10-20; its testing image and merge on his go, after or with the
   3DS.
3. **The Vita:** PR #523's answers; "no, for now", upstream first. His
   word on the support comment on Vita3K/Vita3K#3707.
4. **Words from him, one at a time, no work:** #253 (close with the
   drafted comment in PROJECT.md's #253 record: the misses are gone on
   gamescope 3.16.31; "reopen on a report"); #484's "a drive unplugged
   during Sign out"; #181 and #215 (he is unsure of both).
5. **The Vectrex stall:** no fix; the next step is his evening journal of
   2026-10-19 read for what ran just before each stall (PROJECT.md, the
   Vectrex record under "The 4K misses measured again"). Only if it comes
   back.
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
