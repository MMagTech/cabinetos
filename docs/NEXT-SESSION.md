# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. #150 and #163 are merged (#214): built-in games run on the fast
cores, and any game runs on Bazzite's performance profile, balanced on Home.
Recorded in PROJECT.md after open question 31. Check `bootc status` on the A9
for the image it runs. **Never tell MMagTech something is unrecoverable or
safe to delete before every cause is checked** (memory).

## Next: #63 phase 1, the dial and the fixed tables

The design is in PROJECT.md ("REVISED, MMagTech 2026-10-01") and on #63.
**Proposals discussed 2026-10-02 are on #63 marked PROPOSED, not decided**
(check at every start, record-only as a project stage, a heads-up when a game
is lowered, per game only, the setting as a ceiling); settle them with him
before building phase 3, and record in PROJECT.md only what he decides.

1. **Audit first** (#63, MMagTech 2026-09-30): every core and standalone,
   every picture or smoothness setting, its default, RetroArch's and
   Batocera's, and what each level would set. `docs/CORE-OPTIONS-AUDIT.md`
   is the start. No external files at any level (#63, 2026-10-01).
2. Then the dial (Performance, Balanced, Quality under Display and Sound) and
   the tables, with #209 (VRR and vsync, three TV tests), designed with #73
   and #122 for the pause menu. Walk the scenarios with him first.

Also open, not in the way:
- **#200**, real Wii Remotes, when the hardware arrives; one TV session.
- **#204**, Dolphin's log level, one line, with the next testing push.
