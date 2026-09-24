# CabinetOS Settings: an inventory

This is an inventory for the Settings discussion, not a design. It lists what CabinetOS already needs a place for, what Cabinet's tvOS app shows today, the decisions that limit the choices, and the questions still open. Every row points at the line it came from. Nothing here says what the screen should look like.

Paths: `PROJECT.md` and `NEXT-SESSION.md` are in `docs/`. Code files are in `frontend/src/` unless a path is given. `cabinetos-session` is `system_files/usr/bin/cabinetos-session`.

States used below:

- **BUILT, needs a switch**: works in the code today and is waiting for a row to control it.
- **BUILT, no UI at all**: works today, but only through a flag, an environment variable or a file.
- **DECIDED, not built**: the behaviour is agreed and recorded, nothing is built.
- **NOT DESIGNED**: named as needed, with no agreed shape.

## A. What CabinetOS needs a place for

### Server and accounts

| Setting | State | What is recorded | Source |
|---|---|---|---|
| RomM server address | BUILT, no UI at all | First run writes it to `config/server.json`, `/etc/cabinetos/session.env` outranks it, and no way to change it later is recorded. | PROJECT.md:8912, firstrun.h:104 |
| Add an account from the console | BUILT, needs a switch | `recordPairing` is ready; the missing part is a screen that pairs on a separate client so setup is never re-entered. | NEXT-SESSION.md:2756, PROJECT.md:11647 |
| Remove an account | BUILT, needs a switch | The store already refuses to remove the active account; the row should show that by being disabled. | NEXT-SESSION.md:2759, PROJECT.md:11656 |
| PIN to switch accounts | BUILT, needs a switch | Optional, off by default, entered on a full-screen number pad a controller can drive. | PROJECT.md:11633, NEXT-SESSION.md:2761 |

### Network

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Wi-Fi: see networks, join, forget | BUILT, needs a switch | First run does all of it with no password prompt; Settings is where a Wi-Fi password gets changed later, from the sofa. | PROJECT.md:9295, PROJECT.md:8791, net.h:9 |
| Network status (online, Ethernet or Wi-Fi) | BUILT, needs a switch | `net.h` answers "am I online" as one of its four questions; first run shows "Connected over Ethernet." | net.h:10, PROJECT.md:9061 |

### Controllers

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Add a controller (Bluetooth) | BUILT, needs a switch | The same screen as first run's pairing step, opened from Settings; first run already says "Add one in Settings". | PROJECT.md:8822, setup.cpp:430 |
| Connected controllers | DECIDED, not built | Show what a person recognises as their controller, not a list of devices. | PROJECT.md:1428 |
| Button remapping | DECIDED, not built | Needed because Linux accepts pads SDL has never seen; it asks by position, not by button name. | PROJECT.md:5199 |
| Player assignment | DECIDED, not built | Which pad is player one must be visible and must survive a pad sleeping and reconnecting. | PROJECT.md:5205 |

### Storage

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Kept and cached games | DECIDED, not built | The storage screen shows both, and lets a cached game be kept and a kept game released. | PROJECT.md:913 |
| Games drive | BUILT, no UI at all | Plug a drive in and kept games go there, with no setup screen; the console says once when it is missing. | PROJECT.md:8427, storage.h:102 |

### Display and picture

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Picture quality | DECIDED, not built | One control for the whole console: Performance, Balanced, Quality, with a table per system behind it. | PROJECT.md:10505 |
| HDMI-CEC mode (legacy or native) | DECIDED, not built | The terminal is gone, so the mode switch must exist in CabinetOS's own settings or nobody can reach it. | PROJECT.md:590, PROJECT.md:5241 |
| HDMI-CEC status | DECIDED, not built | Show adapter detected, mode and last CEC event, so a photo of the screen is a useful bug report. | PROJECT.md:580 |
| Output resolution | BUILT, no UI at all | The session takes the display's own preferred mode; `CABINETOS_OUTPUT=WxH` overrides it for a TV that reports the wrong one. | cabinetos-session:82 |
| Integer scaling | BUILT, no UI at all | Off since 2026-09-21 so every system fills the height; `--integer-scale` brings it back for comparison. | PROJECT.md:2257, main.cpp:2871 |
| Glow around the picture | BUILT, no UI at all | `--glow off, normal, strong` exists; recorded as belonging in the pause menu, as in Cabinet. | NEXT-SESSION.md:2404 |
| Shader | DECIDED, not built | Belongs in the pause menu and is chosen per system, not console-wide. | NEXT-SESSION.md:2385, NEXT-SESSION.md:2398 |

### Sound

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Interface sounds on or off | BUILT, needs a switch | Built with its off switch (`sound::setEnabled`); today it is the flag `--ui-sound off`. | sound.h:52, main.cpp:2872, NEXT-SESSION.md:2411 |
| Interface sound volume | BUILT, no UI at all | Default 0.22; the code scales at play time "which is what a Settings slider would want". | main.cpp:2878, sound.cpp:140 |
| Audio output | NOT DESIGNED | Listed as Phase 6 work next to display and resolution handling, with no further detail. | PROJECT.md:5182 |

### Power and idle

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Turn off screen after | DECIDED, not built | 10 min, 15 min (default), 30 min, 1 hour, Never; the dim follows at a third of the choice. | PROJECT.md:5806, NEXT-SESSION.md:557 |
| Today's idle timings | BUILT, no UI at all | Menus dim at 5 minutes and blank at 15; a running game dims at 20 and never blanks. | idle.h:57, idle.h:64 |

### System update

| Setting | State | What is recorded | Source |
|---|---|---|---|
| System Update | DECIDED, not built | Settings > System Update: one check, one button, one reboot, like a PlayStation. | PROJECT.md:11947, PROJECT.md:698 |

### Developer

| Setting | State | What is recorded | Source |
|---|---|---|---|
| File access (SFTP) | DECIDED, not built | A visible row turns it on and shows the address, user name and a password the machine made; off by default. | PROJECT.md:5503 |
| Network name (`cabinetos.local`) | NOT DESIGNED | Whether the console advertises itself over mDNS is still open. | PROJECT.md:5577 |

### About

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Version | DECIDED, not built | About carries the version number. | PROJECT.md:654 |
| Credits | DECIDED, not built | Full credit to Bazzite, Universal Blue, ChimeraOS and the emulator projects. | PROJECT.md:2184 |
| Licence text | DECIDED, not built | The licence text must be readable on the console, in About. | PROJECT.md:2959, NEXT-SESSION.md:2786 |

### Anything else

| Setting | State | What is recorded | Source |
|---|---|---|---|
| Emulator options per system | NOT DESIGNED | Settings must be keyed by system, not by core, or a later settings screen will be expensive to fix. | PROJECT.md:852 |
| Offline mode | NOT DESIGNED | Cabinet has an Offline Mode switch; for CabinetOS one on or off value is recorded as not enough. | PROJECT.md:8218, PROJECT.md:12301 |
| Phone setup page | NOT DESIGNED | Deferred, not adopted; the proposal had it switched off after first run, with a toggle. | PROJECT.md:8618, PROJECT.md:8636 |
| Developer overrides | BUILT, no UI at all | `CABINETOS_STORAGE`, `CABINETOS_DRIVES`, `CABINETOS_APP` and `CABINETOS_OUTPUT` exist for development and testing. | storage.h:81, storage.cpp:166, cabinetos-session:46 |

## B. Cabinet tvOS Settings today

Source: `cabinet/RommApp/RommAppTV/TVSettingsView.swift`. The top level has five rows, and each opens one flat page. There is no "Settings" heading (line 40). Each row shows its current state, so you can see most answers without opening the page (lines 10 to 12). On/off rows are ordinary rows that show "On" or "Off" on the right, not switches (lines 209 to 213).

**Top level** (lines 44 to 97)

1. **Accounts**: shows the current profile.
2. **Controllers**: shows "None connected", the pad's name, or "N connected".
3. **Emulation**: shows "Cores and glow".
4. **Library**: shows "Sort by platform name" or "Sort by folder name".
5. **Server**: shows the server's host name.

**Accounts** (lines 127 to 157)

- Current profile: shows the name, and cannot be selected.
- Switch or add a profile: opens the full-screen switcher.
- Require a PIN to switch: On or Off. It is greyed out until a PIN exists, and says "Set a PIN below first".
- Set a PIN, or Change PIN once one exists: opens a full-screen PIN entry.

**Controllers** (lines 196 to 274)

- Player 1, Player 2, and so on, each with the pad's name. With no pads connected: "No controller connected".
- Rumble: On or Off, on by default.
- Buttons: "Map any button to any input". Opens the remapping page.
- Allow a phone as a controller: On or Off, off by default. While it is off, the TV does not listen on the network at all.
- When that is on: a pairing code with "Enter this code on the phone", or "Ready to pair" or "Phone connected".
- When that is on: Forget paired phones, in red. It asks first: "Each phone will need a new code to join again."

**Emulation** (lines 364 to 422)

- Glow: Off, Subtle (the default) or Strong. "A soft light around the game picture. Currently subtle." The pause menu is where glow mainly lives; this row is a copy of it (line 365).
- Cores: "Speed and accuracy options for each emulator". Opens a page per core.
- Experimental cores: On or Off. "Dreamcast and Nintendo 64. Speed varies by game."

**Library** (lines 433 to 457)

- Names: Platform name or Folder name.

**Server** (lines 469 to 491)

- Connected to: shows the host name.
- Sign out, in red. It asks first: "You'll need to pair this Apple TV with your server again."

## C. Decisions already made that constrain Settings

1. Settings is one of the top bar's destinations, not a button in a corner. It has no "Settings" heading, because the bar already says it (PROJECT.md:2561, PROJECT.md:2573).
2. The row and page shapes are already set: a row is full width with a title, a detail line, an optional value and a chevron; a page is a plain large title then rows, at most 1100 wide (PROJECT.md:2717, PROJECT.md:2363).
3. There is no hidden developer mode any more. File access is an ordinary, visible row, and it gives SFTP, not a shell (PROJECT.md:5503, PROJECT.md:5523, PROJECT.md:5564). Note: PROJECT.md:1484 and PROJECT.md:5185 still describe the old hidden toggle and are out of date.
4. Picture quality is one console-wide control with three choices, never emulator words. It can also be changed in a game's pause menu, and the player's choice for that game wins from then on (PROJECT.md:10505, PROJECT.md:10523, PROJECT.md:10529).
5. The `--ps2-upscale` and `--ps2-aniso` flags are test tools, and no settings screen should be built on them (NEXT-SESSION.md:927).
6. Shader and glow go in the pause menu, not Settings, because the game picture is right there, and shader is chosen per system (NEXT-SESSION.md:2385, NEXT-SESSION.md:2398).
7. "Turn off screen after" is the only idle choice. Pixel shift and how dark the dim goes stay fixed in the code, with no setting (PROJECT.md:5806).
8. System Update is one check, one button, one reboot. Updates are chosen from Settings and never run while a game is running (PROJECT.md:11947, PROJECT.md:5879).
9. The CEC mode must be set from CabinetOS's own settings, it must explain itself, and it must show what state CEC is in (PROJECT.md:590, PROJECT.md:576, PROJECT.md:580). The A9 has no CEC device at all (PROJECT.md:5701).
10. Credits go in Settings > About, not on the boot screen (PROJECT.md:2184).
11. Plugging in a drive is all it takes. There is no choice of storage location and no cache size to set (PROJECT.md:8427, PROJECT.md:4165). PROJECT.md:897 still says "Settings offers a storage location", which is out of date.
12. Keeping a game is done on the game's own screen. Settings only gives the overall picture (PROJECT.md:916).
13. Settings > Add a controller is the same screen as first run's pairing step. Pad 1 chooses the new pad, and the new pad confirms by pressing a button (PROJECT.md:8822, PROJECT.md:8798).
14. The network code will never become a network settings page: no proxies, no fixed IP addresses, no VPNs, no enterprise Wi-Fi (net.h:9).
15. Switching accounts happens from the account chip on Home, not from a picker at boot. The console starts as whoever played last (PROJECT.md:11622).
16. The Power menu (Sleep, Restart, Power off) opens from Home and from the power button, not from Settings (PROJECT.md:5976, PROJECT.md:5994).
17. Words on screen are plain, with no em dashes (NEXT-SESSION.md:488).

## D. Open questions for the discussion

1. Where does the quality control go? It is recorded as "not yet decided" (PROJECT.md:10625).
2. Do add account, remove account and the PIN go in Settings, in the account chip's panel on Home, or both?
3. Should Settings have a way to change the RomM server or sign out, the way Cabinet's Server page does? No source says.
4. Should it be Cabinet's shape (a short list of categories, each opening a page) or one flat list, given how few rows are built today?
5. What does the CEC row show on a machine with no CEC device, like the A9?
6. For file access: should the console advertise itself as `cabinetos.local`, and what should the password look like (PROJECT.md:5577, PROJECT.md:5583)?
7. Is there an offline mode switch at all, given that one on or off value is recorded as not enough (PROJECT.md:12301)?
8. Should interface sound be only on or off, or on/off plus a volume?

## Not in any source, worth asking

These are my ideas. None of them is recorded anywhere.

- A Rumble switch. Cabinet has one; PROJECT.md:1446 only says rumble quality varies by driver.
- Forgetting a paired controller. `bluetooth::forget` exists, but only to retry a failed pairing.
- Using a phone as a controller, as Cabinet offers.
- Sorting names by platform or by folder, as Cabinet's Library page offers.
- Adjusting for overscan or picture size, beyond the fixed safe area in the design.
- Erasing the console back to first run.
- Time zone, clock, or language.
