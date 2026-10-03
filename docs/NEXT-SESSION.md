# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3. **#200, real Wii Remotes, is built on branch `wii-remotes`** and
working on the A9 TV from a hand build (tools/ui-loop.sh with
`--env CABINETOS_WII_BRIDGE=/var/home/cabinet/wiibridge/cabinetos-wii-bridge`)
and two temporary udev rules in `/run/udev/rules.d/` (gone at reboot). The
decisions are in `docs/PROJECT.md` question 35 on branch
`docs-after-picture-quality` (not merged yet). The full test record is on #200.

Passed on the TV, 2026-10-02/03, MMagTech's TechKen copies: pairing (Wii PIN
refused, `0000` taken), reconnect, lights, menus by Remote, unlock, hold HOME,
pointer, Nunchuk games through the bridge (Geometry Wars, Donkey Kong, Wild West
Guns), two Remotes and a pad in Mario Kart, a Remote switched off mid-game (no
crash, Nunchuk right after it comes back), idle switch-off at Home.

## Next

1. **Test the bridge's setup replay.** It is installed at
   ~/wiibridge/cabinetos-wii-bridge but has never run: the console app was not
   restarted after it was copied in, so the 2026-10-03 09:55 test ran the old
   bridge (its log says "extension set up again", not "N setup command(s) sent
   again") and the pointer was way off after the power cycle. Restart the
   console app on Home (tools/ui-loop.sh --no-build with the --env above), then
   in Wild West Guns switch the Remote off and on mid-game; the pointer should
   be right. If it is still off, the camera setup is not being captured or needs
   slower pacing.
2. **Put #200 into the image**: build `wiibridge/` in the builder and install
   it as `/usr/libexec/cabinetos-wii-bridge` (GPL, its own folder); add the two
   udev rules, the search service, the polkit rule (already in system_files,
   listed in build.sh except the udev rules); dry-run on the A9 as `cabinet`
   (lessons: dry-run image scripts first). Then "Pair a Wii Remote" end to end.
3. **Cemu (Wii U)**: it can see a Remote through the stand-in (Nintendo's id);
   it still has to be told to use one in its controller profile.
4. Judge the new Settings > Controllers > "Wii Remotes" panel on the TV.

Known, recorded on #200, not chased: after a game's 5-minute idle drop a button
does not bring the Remote back (Dolphin ignores it since 2019; libretro's
Dolphin also crashes if a held Remote vanishes, which the bridge avoids); the
`+` join prompt in Mario Kart; Bit.Trip Beat's paddle jitter (copy's sensor?);
the Remote that came back on by itself 2026-10-02 22:23; the one game freeze
2026-10-02 22:42. MMagTech does not want upstream reports for now.
