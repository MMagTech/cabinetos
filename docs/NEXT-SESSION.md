# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-09 evening: #304 is on PR #306 (branch `stream-steam-return`), with
testing image **2026.10.09.6** building from abb93e4. It carries our own
Sunshine build (one patch: pick again when its plane empties), Home starting
composed during a stream, Steam's composing held on, the pads helper fix, and
tonight's fix (a session ended by the idle no longer leaves the TV's
controllers set aside). The loop test passed with MMagTech this morning. The
A9 (Wi-Fi, **192.168.1.109**, `CABINETOS_A9=cabinet@192.168.1.109`) runs
2026.10.09.4, restarted at 20:47; it takes updates only from its own System
update screen (the PIN is his to enter); never sudo over SSH.

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order. Lessons file:
testing.md, then image-and-ci.md.

1. **Finish #304.** Check run `gh run list --branch testing` built
   2026.10.09.6. MMagTech updates from System update; that restart is the
   test's restart, so the controller stays off afterwards. Confirm from the
   A9 log the new version and our Sunshine run (`strings /usr/bin/sunshine |
   grep "has had no framebuffer for"`). His test: stream straight in, Home,
   Steam, a game, back to Home, picture on the phone throughout, no unit
   failed. Then merge only on his go, close #304, delete the branch with his
   go. Then the Sunshine pull request: the text is in the PR #306 thread's
   history; Sunshine's AGENTS.md forbids AI agents opening PRs at
   LizardByte, so it goes on his fork and he opens it (ask about the fork).
2. **Review #307** with him: the showcase page, README and wiki (#190). Two
   stills show his Wi-Fi name and LAN addresses; Pages needs switching on.
3. **Signed images (#135).**
4. **A diagnostic report (#195).**
5. **Licences checked and the full texts shipped (#120).**

Also in the milestone: booting with the TV off (#268). After first release:
#290, #283, #303, and #305 (opening Steam sometimes takes 25 s; he said not
to spend time on it).

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
