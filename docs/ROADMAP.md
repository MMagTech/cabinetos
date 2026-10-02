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
3. **Settings: picture.** Picture quality (#63), the pause menu's per-system
   picture and controller options (#73, which also own the PS1 DualShock and
   the N64 Rumble Pak), and shaders and CRT looks (#122, to be designed).
   **Then Steam (#223)**, straight after #63's three phases (MMagTech,
   2026-10-02): one entry that hands over to Bazzite's Big Picture session,
   audited first and built on his go. Moved up from After first release
   because it is the selling point that brings testers to the alpha.
4. **Offline play.** Kept games play with no server (#88).
5. **RetroAchievements** (#74). Each account signs in with its own login.
   After offline, because achievements earned offline must wait and send.
6. **UI polish.** The wording pass and notification length (#110), the
   Library showing only systems this console can play (#117), storage the
   console cannot see (#133), sleep that really works on this hardware
   (#132), the SELinux denials at boot (#119), and two small doc chores (#81,
   #118).
7. **Ready to ship.** The installer (#105 to #108, #136), a release image
   without the development shell (#134), signed images (#135), CabinetOS named
   in the system's version info (#137), licences checked and shipped (#120).
8. **Testing at the TV.** The release gate: covers and art on the TV (#112), a
   PS2 save reaching the server (#113), Cabinet on Apple TV loading our states
   (#114), players 3 and 4 (#115), the owed hardware tests (#140), and last,
   the first-hour walk-through as a new user on the real installer (#111).

**After first release:** everything in that milestone on GitHub, including
the Dreamcast crash (#151), the performance profile (#150), rewind's cost
(#168), multi-disc swapping (#162), PCSX2's per-game fixes (#154), the RomM
5.2/5.3 features (#158 to #161) and the system tuning ideas (#163 to #167).

Done: Settings (#57 to #72, except #63), the emulator block (#82 to #90), the
in-game features (#76 to #80).

## Decided against

Kept here so nobody proposes them again: Atari Jaguar and ColecoVision (their
number-pad controllers), HDMI-CEC (shelved 2026-09-24), static IP, screen
recording (encoding cost on integrated graphics, and clips would compete with
the game cache).
