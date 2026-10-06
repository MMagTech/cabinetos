# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-06: RetroAchievements (#74, milestone 5) and the Search fixes (#277)
are on `retroachievements`, PR #278, judged on the TV that evening and pushed
to `testing`. **Merge only on MMagTech's word**, after the testing image is
verified from the A9 and he has picked its checks. The decisions are
docs/PROJECT.md open question 38.

The A9 sits at the LG C1, on Wi-Fi at **192.168.1.109**: run
`tools/ui-loop.sh` with `CABINETOS_A9=cabinet@192.168.1.109`.

## Next

Milestone 6, UI polish (docs/ROADMAP.md), in this order:

1. **Wording pass over every screen, and notification length (#110).**
   Lessons file: frontend.md (words on screen).
2. **Sound that dies until a restart (#228):** the TV's HDMI audio chip
   failing to power down.
3. **Offer the sleep this machine can really do (#132).**

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). After a testing image installs,
verify it from the A9 and list the possible checks; MMagTech decides which.
