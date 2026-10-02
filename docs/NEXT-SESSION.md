# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`
(milestone 3's order is written there).

## Where things stand

Milestone 3. **#63 phase 1 is built on `picture-quality`** with #73, #209
and #217, and pushed to `testing` (PROJECT.md, "BUILT ON picture-quality").
MMagTech judged most of it on the TV with the test build; **still to judge on
the testing image: a PS2 game with a widescreen patch, at Quality** (Burnout
3 has one). Merge only on his explicit go, then close #63's phase 1 items,
#73, #209, #217 and delete the branch (and `docs-after-fast-cores`, whose
commits it carries).

## Next

1. **Judge the testing image** with MMagTech, PS2 widescreen first; then
   `tools/check-applied.sh` should show no FAIL (patches.zip now there).
2. **#63 phase 2, the machine class**: what the hardware reports at first
   boot sets the starting level (design in PROJECT.md, REVISED 2026-10-01;
   the PROPOSED items on #63 are still undecided). Walk the scenarios first.
3. Then before phase 3: #210, #168 (only: the drop rule ignores rewind
   snapshot frames), #221 (measure first).

Also open, not in the way:
- **#223, Steam:** the audit is on the issue (verdict moderate; cardwire is
  the blocker; four points of the brief to settle with MMagTech). Built after
  #63's phases.
- **#200**, real Wii Remotes, when the hardware arrives.
- **#204**, Dolphin's log level, one line, with the next testing push.
- The A9 has a blank `bios/dc/vmu_save_A1.bin` from today's tests; harmless
  (the next Dreamcast launch files it as unattributed). MMagTech removes it.
