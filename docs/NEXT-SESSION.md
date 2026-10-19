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

**2026-10-19.** `latest` and `testing` are **2026.10.19.2** (revision
a760620, digest sha256:b49fd4925ef5...), #517, "Overnight batch", merged on
his go after his TV check and promoted. Built unattended overnight by nine
sessions; it closed 22 issues. #473 and #222 were reopened with what is
left on each. Judged on the TV: Genesis 4:3, the grid's cover lift, the PS2
"Disc can't be read" ending, the Restart fade, achievement progress after a
state load, PS3 "VSync Mode: Full" (his call: stays on, revisit only on a
report), N64 without the hitch. 39 issues are open. His server can move to
RomM 5.4 (#516 is out; database backup first). The A9 is on the testing
channel; its PIN is off. The machines' addresses are in his local notes,
which CLAUDE.md loads; the tools read them from the environment
(`CABINETOS_A9`, `CABINETOS_KEY`, `CABINETOS_VM`, `CABINETOS_RELAY_UP`).

**His rules from 2026-10-18:** no issue is opened unless he asks for one
(findings go in the pull request or PROJECT.md); FSR for Xbox 360 stays off
(PROJECT.md, question 34). His decisions of that night are in #517's body.

## Next

1. **The priority list of the 39 open**, as one short list for his yes or
   no, grouped as in the 2026-10-19 morning message: ready for the next
   overnight batch with no decision (#480, the rest of #481, #482, #487,
   #488, #203, #305); needs the TV or a device; waiting on others; needs
   his decision; suggested to close (#288, #339, #493, #398, and #490/#492's
   build-script items). Then run the "no decision" group as the next
   overnight batch, the same way as #517.
2. **The automatic checks report only until 2026-10-25**, then fail a pull
   request: read their first week's reports and fix what they find
   (CONTRIBUTING.md, "What CI checks").
3. **#511's last proof:** the next image built with no change under
   build_files/ or system_files/ must keep the 70 MB OS layer's digest (the
   254 MB splash layer already held between 19308fe and a760620; method in
   #517).
4. **Review the docs for Cabinet Go**: Cabinet is now the pocket version
   for handhelds, unfinished; the docs that call it "the Apple TV app"
   (CONTRIBUTING.md, docs/CABINET.md, PROJECT.md's banner, README.md, the
   site) need reviewing.
5. **The monthly health check** starts after item 2.

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
