# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-05: offline play (#88) and the cartridge-save fix (#259) are built on
branch `offline-play`, tested headless and on the A9 with a wrong address, and
not merged or pushed to testing. What was built and verified is in
docs/PROJECT.md, open question 22, "Built, 2026-10-05". The A9 is back on its
image. #228 (sound chip power) is still open.

## Next

1. **Judge offline play on the TV, then merge #88 and #259.** Deploy with
   `tools/ui-loop.sh --env CABINETOS_ROMM=192.168.1.10:6099` (offline from
   boot; link the covers folder as docs/lessons/testing.md says). Judge: the "Offline" chip's look, the lift from the startup screen
   at 15 s, Home, Library, Search, a game, switching person. Then the #259
   test with a real in-game save on a cartridge game, offline and then online
   before the retry. For the server coming back, use the relay in
   docs/lessons/testing.md. After his go: push to testing, judge the image,
   merge on his word.
