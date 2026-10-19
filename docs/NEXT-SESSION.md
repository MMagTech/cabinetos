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

**2026-10-19, evening.** `latest` is **2026.10.19.2** (#517). `testing` is
**2026.10.19.3** (revision 2b115cb), the day's batch: PR #519, "Batch 2026-10-19: Wii Remotes, Play on a slow server, archive folders, stranded Xbox saves, Steam's exit, What's new",
waiting on his TV check and merge. Seven sessions built it on separate
files while he was at work, one combined it, built it in the builder, ran
every self-test and smoke-tested it headless on the A9 through the
read-only relay. Nothing under `build_files/` or `system_files/` changed,
so this image is #511's last proof: the 70 MB OS layer's digest must match
2026.10.19.2's (the PR says whether it did). The morning's review of the 37
open issues, one at a time with him: eight closed, every other decision
written on its issue; 29 open now, 24 once the PR merges. His server has
been on **RomM 5.4.0** since 2026-10-19; the batch's headless runs were the
first proof of #516 on it. The A9 is on the testing channel; its PIN is
off. The machines' addresses are in his local notes, which CLAUDE.md
loads; the tools read them from the environment (`CABINETOS_A9`,
`CABINETOS_KEY`, `CABINETOS_VM`, `CABINETOS_RELAY_UP`; the Mac's shell
profile does not set them, so export them per command).

**His rules from 2026-10-19:** an unattended run never waits on a prompt
(every session's brief says to skip what needs approval and carry on, and
the parent sets a timed fallback to combine whatever landed); decisions
are put to him one at a time, mechanics first, never with options he did
not ask for; an opt-in, disclosed upload to a site under his control is
his exception to "the console connects only to RomM and GitHub" (#215).
No issue is opened unless he asks; findings go in the pull request or
PROJECT.md.

## Next

1. **His TV check of 2026.10.19.3, then the merge on his go** (the list is in
   PR #519, "For the TV"). After the merge: close nothing by hand (the
   body closes #480, #481, #482, #488 and #329), delete the batch branches
   (`claude/ovn4-*`, `claude/research-*`, `ovn4-2026-10-19`) and their
   worktrees, and record #511's verdict on that issue's thread if the PR
   had none.
2. **The automatic checks report only until 2026-10-25**, then fail a pull
   request: read their first week's reports and fix what they find
   (CONTRIBUTING.md, "What CI checks").
3. **The next batch, the one that may touch `system_files/`** (once #511
   is proven): #305's 25 s stall, which is gamescope's own exit waiting on
   logind (the Steam session's findings are in PR #519: in
   `cabinetos-session`, give gamescope 2 s once its app is gone, then stop
   it and carry on; try it with `ui-loop.sh --session-script` first);
   #492's last item (STALE-187, the flatpak script's unset overrides);
   #484's small leftovers (its first comment); #253's measurements and the
   `--rt` trial; the Wii bridge's `make test` as a step in
   `build-frontend.yml` (one line, and drop "CI does not run it yet" from
   CONTRIBUTING.md). With the next real core change: #490's comments in
   the core and PCSX2 build files.
4. **Review the docs for Cabinet Go**: Cabinet is now the pocket version
   for handhelds, unfinished; the docs that call it "the Apple TV app"
   (CONTRIBUTING.md, docs/CABINET.md, PROJECT.md's banner, README.md, the
   site) need reviewing.
5. **The monthly health check** starts after item 2.

## Waiting, not for the next session unless MMagTech raises them

- **His decisions:** #313 (Select on a stream; the Home-always fix is laid
  out on the issue), #198 (the Vita research says no for now, and no gyro;
  PROJECT.md question 49), #339 (the multi-disc audit: 15 of the 30 swap
  discs mid-story; the table is on the issue and in question 34), #215,
  #181 and #224 (each with its design on the issue), #487's
  BASE_UPDATE_TOKEN secret and fork design.
- **Needs the TV or a device:** #473 (a press while an emulator of its own
  has the screen), #411 (Moonlight), #381 (the HDMI cable out), #290 (the
  C1, before a release), #228 (the measurement in PROJECT.md section 46
  when the sound dies again), #132 (the Bluetooth wake test).
- **Waiting on others:** #297 and #390 (Sunshine), #346, #353 and #362
  (RomM's save channels; revisit mid-January 2027), #222 (a Flycast
  refusal seen in play), #203 (libretro/dolphin#508, no reply since
  2026-10-01; the console's half waits for it).
- **The game page's launch messages, later** (MMagTech, 2026-10-19, at the
  TV): "Disc can't be read" sits as a loose grey line under Download, as do
  all ten or so reasons Play or Download fails ("Couldn't start this game",
  "Not enough space", "No PS2 BIOS on your server", the keys and licence
  ones). He wants their placement (the Play row's right end, in amber, was
  offered; or the bottom pill) and their wording reviewed together, with a
  way to cycle through each on the TV first, like `--notice-gallery`: he
  has never seen half of them. Not an issue.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
