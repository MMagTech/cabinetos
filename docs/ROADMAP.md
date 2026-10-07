# CabinetOS roadmap

**The goal is the first release.** The order below was set with MMagTech on
2026-09-28, and it is the order work is done in. Each step is a numbered GitHub
milestone holding every open issue for it; anything not needed for the first
release is in **After first release**. A new issue goes into a milestone when
it is filed. Anything found along the way that is not a release blocker goes
into After first release rather than jumping the queue.

Milestones: https://github.com/MMagTech/cabinetos/milestones

The reasoning behind any step lives in `docs/PROJECT.md`; what the next
session picks up is `docs/NEXT-SESSION.md`.

## In order

1. **Emulators. Done 2026-09-28.** PS2 on a fresh install (#153) was fixed;
   the Dreamcast crash (#151), full CPU speed during games (#150, measured: no
   visible benefit on the A9) and rewind's snapshot cost (#168) moved to After
   first release.
2. **New systems**, in MMagTech's order: the shared foundation for emulators
   that run as their own programs (#169), then Switch (#170), PS3 (#171), Xbox
   (#172), Xbox 360 (#192), Wii (#173) and Wii U (#174). Xbox 360 moved ahead
   of the Wiis on 2026-09-29, after it ran well on the A9. Before picture quality, because that
   is one setting across every system.
3. **Settings: picture**, in this order (set with MMagTech 2026-10-02):
   1. ~~**#63 phase 1**, with #73, #204, #209 and #217.~~ **Merged in #225,
      2026-10-02.**
   2. ~~**Wii Remotes (#200)**.~~ **Merged in #232, 2026-10-03**: pair once,
      the Wii bridge in the image, judged on the A9 TV.
   3. ~~**Steam (#223)**~~ **Built on `steam-handoff` and judged on the A9
      TV, 2026-10-03/04**: "Switch to Steam" in the Start menu, its own
      slice of the drive (PROJECT.md open question 37).
   4. ~~**PS2's picture without the copy (#226)**~~ **Merged in #239,
      2026-10-04**: its own window, as the separate emulators are shown;
      Quality 4x with 16x filtering. Upstream declined the other route
      (PCSX2/pcsx2#15040).
   5. ~~**ext4 for extra drives, shared with Steam (#236)**~~ **Merged in
      #240, 2026-10-04.**
   6. ~~**#63 phase 2**, the machine class, with #209 (vsync on).~~
      **Built on `machine-class` and judged on the A9 TV, 2026-10-04**: the
      starting level from the graphics chip, N64 Balanced 2x, PS3 Quality
      1080p, Xbox 360 720p. An Ultra level for stronger machines is #245.
   7. **Shaders and CRT looks (#122)**, moved ahead of phase 3 (MMagTech,
      2026-10-04: "at least that's something fun"). Discussed before
      anything is built; not a copy of Cabinet's.
   8. **Before phase 3:** the slowed A9 (#210), the drop rule ignoring rewind
      snapshot frames (#168), whether built-in cores repeat or drop frames
      (#221).
   9. **#63 phase 3**, the drop rule, record-only first.
   Also in the milestone: the gamescope flag check on base updates (#227).
4. **Offline play.** Kept games play with no server (#88).
5. **RetroAchievements** (#74). Each account signs in with its own login.
   After offline, because achievements earned offline must wait and send.
6. **UI polish.** The wording pass and notification length (#110), then
   sound dying until a restart (#228). Sleep (#132) moved to After first
   release.
7. **Ready to ship.** First the Wii Remote's speaker (#276, a release
   blocker unless impossible) and a discussion of the Wii Menu and Miis
   (#275). Then the installer (#105 to #108, #136), a release image
   without the development shell (#134), signed images (#135), CabinetOS named
   in the system's version info (#137), licences checked and shipped (#120).
8. **Testing at the TV.** The release gate: covers and art on the TV (#112), a
   PS2 save reaching the server (#113), Cabinet on Apple TV loading our states
   (#114), players 3 and 4 (#115), the owed hardware tests (#140), and last,
   the first-hour walk-through as a new user on the real installer (#111).

**After first release:** everything in that milestone on GitHub, including
the Dreamcast crash (#151), the performance profile (#150), rewind's cost
(#168), multi-disc swapping (#162), PCSX2's per-game fixes (#154), the RomM
5.2/5.3 features (#158 to #161), the system tuning ideas (#163 to #167),
Wii U Remotes in Cemu (#231) and controller batteries (#230).

Done: Settings (#57 to #72, except #63), the emulator block (#82 to #90), the
in-game features (#76 to #80).

## Decided against

Kept here so nobody proposes them again: Atari Jaguar and ColecoVision (their
number-pad controllers), HDMI-CEC (shelved 2026-09-24), static IP, screen
recording (encoding cost on integrated graphics, and clips would compete with
the game cache).
