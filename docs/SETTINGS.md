# CabinetOS Settings: what was decided

Decided with MMagTech on 2026-09-24. This is the final state only, organised
the way the screen is. The reasoning, and the options tried and dropped, are in
`docs/PROJECT.md`, open question 31, in the order they came up. If this file
and question 31 ever disagree, fix this file: it is meant to be the one place
to look.

`docs/SETTINGS-INVENTORY.md` was the starting point for the discussion and is
now history.

**Status words:** **Built** works on the test build today. **To build** is
decided, not built. **Later** is wanted, not now. **Dropped** was decided
against.

**Tracking:** one GitHub issue per feature, under the **Settings** milestone
(https://github.com/MMagTech/cabinetos/milestone/1), labelled `settings` or
`pause-menu`; the **later** ones carry the `later` label and sit outside the
milestone. Discuss on the issue; the PR that builds it closes it and flips its
status here to Built.

**Where the work is:** branch `settings-side-list`, PR #56, **not merged on
purpose**. MMagTech judges it on the television with `tools/ui-loop.sh` first
and says when it is right.

---

## The screen

- **A side list.** Categories down the left, the chosen category's rows on the
  right, both on screen. Moving through the list changes the right side at
  once. Settings is the one screen allowed to differ from Cabinet's shape.
  **Built.**
- **Seven categories:** Accounts, Controllers, Network, Display and Sound,
  Storage, System, About. **Built.**
- **Plain purple background, no game art**, because it is a screen of text.
  **Built.**
- **Rows not built yet are dimmed, say "Not built yet", and focus skips
  them.** This is for judging the layout; they go away as each is built.
  **Built.**
- **Changing category cross-fades the rows**, about 150 ms, no movement.
  **Built.** MMagTech on the TV, 2026-09-24: good.
- **No emulator settings page.** Emulator options are either covered by
  Picture quality, set once by us and never shown, or live in the pause menu
  (see below).

## Moving between the top-bar screens

- **Moving left and right across the top bar switches the screen**, the same
  as the shoulder buttons. Pressing Up into the bar switches nothing. The
  account button still needs A. **Built.**
- **The shoulder buttons work from Search too**, and leaving Search clears it.
  **Built.**
- **The switch:** the old screen dissolves away (300 ms), Search's keyboard
  slides down off the screen, and the new screen's content waits 200 ms then
  rises 16 points while it fades in (300 ms). The keyboard slides back up when
  you return to Search. MMagTech on the TV: *"that slide nailed it."*
  **Built. Do not retune from a screenshot.**
- **Search always has blurred game art behind it**, the last game you were
  looking at, even arriving from Settings. **Built.**

## Accounts

- **No "Playing as" row.** The account button in the bar already says who is
  signed in. **Built.**
- **Add an account** is in Settings and in the account button's panel.
  **Built.**
- **The first account from setup owns the console.** **Built.**
- **One PIN, set by the owner.** With no PIN, every account can do everything.
  With a PIN, these ask for it instead of disappearing, so the owner can act
  while a child is signed in:
  - Wi-Fi changes
  - Sign out and Change server address
  - Adding an account, and removing one
  - File access (turning it on, making a new password)

  Open to everyone: sounds, picture quality, system update.
  **Adding an account needs the PIN when one is set**, from the account
  panel and from Settings alike. MMagTech, 2026-09-24: otherwise anyone
  holding the controller can add themselves. With no PIN, anyone can add one,
  like everything else.
  The same PIN stops anyone switching into the owner's account. **Built, #57**
  (a centred number pad; five wrong tries lock it for 30 seconds).
- **Only the owner sees the PIN's controls.** Signed in as the owner:
  Set a PIN, or Change PIN and Turn off PIN. Anyone else sees one row, "PIN",
  On ("Set by MMagTech") or Off ("Only MMagTech can set one"), with nothing
  to press. MMagTech, 2026-09-24. **Built.**
- **One PIN, not one per person**, kept after comparing consoles on
  2026-09-24: PlayStation and Xbox let each person lock their own account;
  Nintendo and Apple TV (and so Cabinet) have only the one console PIN.
  **Later, if wanted:** an optional lock per account that guards switching
  into it, added beside the owner PIN without changing it.
- **Dropped: offering a PIN when the second account is added.** Built and
  tried on 2026-09-24, then removed. If the owner is signed in and someone
  else has the controller, that person could answer the offer and set a PIN
  the owner does not know. MMagTech: *"just abandon this screen"*. A PIN is
  set from Settings, by the owner, when they want one.
- **Remove an account** is in Settings only. It removes the account from this
  console; the RomM account and its saves are untouched. The account playing
  now cannot be removed. **To build, #58.**
- **RetroAchievements**, a sign-in per account. **Later, #74.**
- **Background colour per account:** a few hand-picked choices (purple, blue,
  green, red, graphite), so the console takes on the colour of whoever is
  signed in. The boot screen stays purple. **Later, #75.**

## Controllers

- **Connected controllers** (which is which player), **Add a controller** (the
  same pairing screen as first run), **Button mapping**. All **to build**:
  #64, #65, #66.

## Network

- **Status** (connected over Ethernet or Wi-Fi, and the address). **Built.**
- **RomM server** address shown. **Built.**
- **Wi-Fi:** one row under Network, showing the network in use ("Off" or
  "Not connected" otherwise). Pressing it (PIN if set) opens a panel listing
  the networks in range, the one in use first ("Connected"), then saved
  ones ("Saved"), then by signal; six at a time, scrolling. The networks were
  rows on the Network page for one build, and MMagTech: in a crowded building
  that list would be long. Choosing one: in use gives Change password /
  Forget; saved gives Join / Change password / Forget; new joins, with the
  on-screen keyboard for a password. Forget asks "Forget <name>?". Change
  password is forget then join. The notice pill says how a join went ("Wrong
  password" when it was). 802.1X networks are not offered. **Built, #59.**
- **Addresses are automatic only.** A fixed address is set on the router.
  **Dropped: manual IP**, unless people ask, then it is one "Advanced" page.
- **Change server address:** the same server at a new address. The console
  proves it is the same server by asking the new address who the account's
  sign-in token belongs to; only the server that issued a token accepts it. A
  different server is refused and pointed at Sign out. **To build, #60.**
- **Sign out:** one confirmation screen. All cached and kept games on this
  console are removed; saves are safe on the server. Anything not yet
  uploaded is tried quietly first, and mentioned only if it failed (by design
  there never is any). Everything tied to that server is cleared: its games
  and the accounts. Wi-Fi and controllers stay. Then back to first run's
  server step. **To build, #61.**
- **One server at a time.** A friend's server is sign out, then sign in.

## Display and Sound

- **Picture quality:** Performance, Balanced or Quality for the whole console,
  and a game's pause menu can override it for that game. **To build, #63.**
- **Interface sounds:** one row changed with left and right: Off, Quiet,
  Medium, Loud, and remembered in `config/settings.json`. Left and right
  change it; Back returns to the list. Judged on the TV 2026-09-24.
  **Built, #62.**
- **Turn off screen after:** 10 min, 15 min (to start), 30 min. The screen
  always dims at 5 minutes. Saved in `config/settings.json`. **Built, #71.**
  Under Display and Sound, not System: it is about the screen. MMagTech,
  2026-09-24, on the TV, which also **dropped Never and 1 hour** (on an OLED
  a lit menu is burn-in; half an hour is long enough) and **the dim at a
  third of the choice** (odd times like 3 min 20 s; 5 minutes is one rule).
- **HDMI-CEC: dropped.** No adapter will be bought to test with, and standby
  already covers what it was for. This reverses the earlier "CEC is a
  requirement".

## Storage

- **Drives, with their space:** the main drive is **CabinetOS**, another
  internal drive is **Internal**, a USB drive is **External**; two of one kind
  are told apart by the drive's name. **Built.**
- **Eject** for a USB drive: finishes or stops anything writing to it, then
  says "Safe to unplug". **To build, #67.**
- **Kept and cached games:** see what is on the console, keep or release.
  **To build, #68.**
- **File access** (under Storage):
  - SFTP only, no shell. Off until turned on.
  - User name `cabinet`, fixed.
  - Password made by the console the first time it is turned on, kept through
    restarts, shown in plain text on the page, changed only by **New
    password**.
  - From a computer you see only the console's saves and games folders, and
    only the `CabinetOS` folder on an external drive. Nothing of the system.
  - **To build, #69.** Setting your own password: **later**, if people ask.
- **Plugging a drive in and it being used has not been tested on the A9 with a
  real drive yet.**

## System

- **System update:** one check, one button, one restart. **To build, #70.**

## About

- **Version:** CabinetOS has **no version number of its own yet**, only
  Bazzite's. One has to be decided. **To build, #72.**
- **Credits** (Bazzite, Universal Blue, ChimeraOS, the emulator projects) and
  **Licences**, readable on the console. **To build.**

## In the pause menu, per system (not Settings)

Every option Cabinet offers is kept, only moved here, because the picture is
right there and the choice is about a system:

- **Virtual Boy:** 3D glasses (Off, red and blue, red and cyan, red and
  electric cyan, green and magenta, yellow and blue) and all eight screen
  colours. MMagTech's condition: every one of Cabinet's choices.
- Game Boy colours, GBA screen colours and blending, Atari 2600 flicker
  blending, Vectrex overlays.
- Controller type: Mega Drive 3 or 6 button, PC Engine 2 or 6 button.
- Shader and glow, as already decided.

**To build, #73.**
