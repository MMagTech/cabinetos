# CabinetOS roadmap

What comes next, in order. Set by MMagTech on 2026-09-24. Each step is a GitHub
milestone; its issues are the work, and a milestone gets its issues when we
reach it, the way Settings did.

Milestones: https://github.com/MMagTech/cabinetos/milestones

The detail and reasoning behind any step lives in `docs/PROJECT.md`; the
state of play for the next session is in `docs/NEXT-SESSION.md`.

## In order

1. **Settings.** Decided in discussion on 2026-09-24; the final answers are in
   `docs/SETTINGS.md`, one issue per feature (#57 to #73). The layout is built
   and being judged on the television (PR #56, not merged until MMagTech says
   it is right).
2. **UI polish.** The rest of the interface: the harsh cut from boot to Home,
   and the pass over on-screen text. Queued with it, not now: the Library
   showing only systems the console can play, as Cabinet's tvOS app does.
3. **Emulators.** The cores still to bring in, and fixing the ones already in:
   N64 textures (#82), GameCube audio (#83), NES audio sync (#84), Dreamcast
   on the A9 (#85, confirm first), N64 save states (#86), PSP's exit crash and
   state (#87), finishing the per-system options (#89), and a warning when a
   BIOS is missing (#90).
4. **In-game features.** From a brainstorm on 2026-09-24: a modifier hotkey
   layer (#76), fast forward (#77), rewind for libretro cores (#78),
   screenshots uploaded to RomM (#79), and a save state undo (#80). Each issue
   carries the decisions and the questions to settle when it is built. After
   the emulator work because they touch every emulator; before the TV tests so those
   test them too.
5. **Testing at the TV.** Once the UI is finished and every core is in: the
   pad tests at the television (account switching, save and load state,
   Dreamcast and PSP states, the black-screen fix, the boot and covers), and
   MMagTech's check that states made on CabinetOS load in the Cabinet apps.
   Not earlier.
6. **Installer ready to ship.** What a real person hits installing CabinetOS,
   found testing it on 2026-09-19, and the fixes agreed then. Last, because it
   is what is fixed before CabinetOS is handed to anyone else. Not yet broken
   into issues.

Steps 4 and 6 were placed by Claude on 2026-09-24 at MMagTech's word
("whatever you feel best"); move them if that changes.

## Not placed yet

- **Offline: kept games play with no server** (#88). Designed in PROJECT.md
  open questions 22 and 29, not built. Big enough to be its own step.

## Later, not scheduled

Issues labelled `later`: RetroAchievements sign-in per account (#74) and a
background colour per account (#75).

## Decided against

Kept here so nobody proposes them again: Atari Jaguar and ColecoVision (their
number-pad controllers), HDMI-CEC (shelved 2026-09-24), static IP, screen
recording (encoding cost on integrated graphics, and clips would compete with
the game cache).
