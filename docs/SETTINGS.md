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

**Where the work is:** on `main`. Each item is judged on the television with
`tools/ui-loop.sh` before its PR merges.

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
  - Developer access (turning it on, making a new password)

  Open to everyone: sounds, system update. **Picture quality**, with a PIN
  set, is seen only by the owner, in Settings and the pause menu, and
  never asks; with no PIN, by everyone (MMagTech, 2026-10-02, #63).
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
- **RetroAchievements**, a sign-in per account, **opt-in** (#74, decided with
  MMagTech 2026-10-06). One row, "RetroAchievements", its value "Sign in" or
  the username. Pressing it asks for the username, then the password, on the
  keyboard (hidden like a Wi-Fi password; a wrong one is said in the field).
  The password is sent once and never kept; the token RetroAchievements gives
  back is, privately, beside the person's RomM token. Signed in, pressing it
  offers **Sign out**. No PIN: it is the person's own account. The username is
  typed, not taken from RomM (RomM's `ra_username` is a field a person fills
  in separately, and was empty for MMagTech). **Somebody who never signs in
  never sees RetroAchievements anywhere else.** Signing out takes the game
  pages' cups, the unlock cards and the Achievement sound row away with it.
  **Softcore only**: nothing hardcore is built, hidden or otherwise (decided
  2026-10-05). **Built on `retroachievements`, judged on the TV 2026-10-06.**
- **Background colour per account:** a few hand-picked choices (purple, blue,
  green, red, graphite), so the console takes on the colour of whoever is
  signed in. The boot screen stays purple. **Later, #75.**

## Controllers

Decided with MMagTech 2026-09-26, from the things people will actually do.

- **Up to four players, one per controller** (#64). Until then a game heard
  only the first pad; every pad walked the menus, and still does. A system
  with fewer ports uses the first ones. The keyboard is player 1 too.
- **Outside a game the numbers close up**: the pads that are on are 1, 2, 3
  in the order they connected. Both pads go to sleep, and whichever one is
  picked up next is player 1.
- **During a game a pad that goes off keeps its number** while any other pad
  is still on, and gets it back when it returns (known by its serial, the
  Bluetooth address). **If every pad goes off, the first one back is player
  1.** Leaving the game closes up any number still held.
- **One row per controller: its name, with "Player 1" as the value**, the
  shape of every other setting. (Built first as "Player 1" over the name;
  MMagTech: nothing said the number could be changed.) **The pad last
  pressed has a steady dot before its "Player N"**: that is how two
  identical pads are told apart. (A flash on every press was tried first; it
  strobed while navigating and read as a bug.) Pads with player lights show
  their number on the pad.
- **A pad is named as it names itself.** An 8BitDo in Switch mode reads
  "Nintendo Switch Pro Controller", and that is right: it is in Switch mode.
  (Naming pads after their maker, from the Bluetooth address, was built and
  taken back out the same day; MMagTech preferred what the pad says.)
- **Pressing a row: "Make player N"** for each other pad (the two swap; a
  preferred pad keeps its number while both stay on) **and Forget** for a
  Bluetooth pad, with "Forget <name>?" and Cancel focused. No PIN. A lone
  wired pad has nothing to press, so its row only shows.
- **Add a controller** (#65): **a window over Settings, like Wi-Fi's**, no
  PIN. (First run's full pairing screen was built for it first and
  compared: it felt like leaving Settings. First run keeps that screen.)
  - Title "Add a controller", line "Put a controller into pairing mode".
  - It opens one row tall, "Looking for controllers…" dimmed in it, and
    **grows smoothly** as devices arrive; the width never changes. (Opening
    small then jumping read as a second window; opening six rows tall left
    an empty band.)
  - **Only devices heard since it opened**, each listed the moment it is
    heard, in ten-second listening rounds. Controllers sort to the top;
    other named devices (lights, TVs) stay listed, because a controller
    that does not say it is one must not go missing (MMagTech). Pads
    already playing are not listed.
  - Choosing one: its row says "Pairing…". Success: the title becomes
    **"Paired successfully"**, the line "Press a button on it", and the new
    pad's first press closes the window with "<name> is player N". Failure:
    "Couldn't pair. Try again".
  - B closes it at once, stopping a scan or pairing in flight.
  - A wired pad needs none of it: plugged in, it is the next player (and
    its first press closes the window if it is open).
- **No Button mapping screen** (#66, decided with MMagTech 2026-09-26). An
  unrecognised pad is handled by the console, not the person: the image
  carries the community's controller list (SDL_GameControllerDB, 491 more
  controllers than SDL knows alone), refreshed by a weekly pull request, and
  a pad reported missing is added to it. **Personal remapping is parked**:
  every system already plays with RetroArch's default layout by button
  position, and a remap has no clean owner with four players.
- **A playing pad dropping out opens the pause menu**, as on Switch and
  Apple TV (MMagTech: yes). Any pad can resume.
- **Rumble: Off / On, on by default** (#149, decided with MMagTech
  2026-09-27: match Cabinet). One switch for the console, every pad and
  every system with motors: N64's Rumble Pak, PlayStation's DualShock (PS1
  and PS2), GameCube, Dreamcast's Puru Puru Pack, Game Boy rumble carts.
  Below Add a controller, above In-game shortcuts. Motors stop in the pause
  menu, while a state loads, during rewind and when the game ends; every
  send lasts 250 ms and is renewed while it holds, so a crash cannot leave
  a pad buzzing. Pads without motors ignore it.

## Network

- **Status** (connected over Ethernet or Wi-Fi, and the address). **Built.**
- **Wi-Fi:** one row under Network, showing the network in use ("Off" or
  "Not connected" otherwise). Pressing it (PIN if set) opens a panel listing
  the networks in range, the one in use first ("Connected"), then saved
  ones ("Saved"), then by signal; six at a time, scrolling. The networks were
  rows on the Network page for one build, and MMagTech: in a crowded building
  that list would be long. Choosing one: in use gives Change password /
  Forget; saved gives Join / Change password / Forget; new joins, with the
  on-screen keyboard for a password. Forget asks "Forget <name>?". Change
  password is forget then join. The PIN is asked once per visit to Settings.
  The password is masked, the character just typed shown for a moment, with
  a "show" key. While it joins, the keyboard stays up and its field says
  "Joining…"; a wrong password empties it to "Wrong password" (shaken) until
  the next try is typed. A failed join leaves no saved network behind. The
  pill says "Connected to <name>". No signal bars: tried and dropped the
  same day, since a person joins their own network whatever its strength
  (MMagTech). 802.1X
  networks are not offered. First run's Wi-Fi step behaves the same way.
  Worked out on the TV with MMagTech, 2026-09-24. **Built, #59.**
  **Later, if asked for:** joining a network that hides its name (first run
  can; Settings cannot yet).
- **Addresses are automatic only.** A fixed address is set on the router.
  **Dropped: manual IP**, unless people ask, then it is one "Advanced" page.
- **RomM server** is one row showing the address. Pressing it (PIN if set)
  opens a panel titled with the address: Change address, Sign out, Cancel.
  The Wi-Fi row's shape. **Built, #60 and #61**, judged on the TV with
  MMagTech, 2026-09-25.
- **Change server address:** the same server at a new address. The keyboard
  opens holding the current address; Done checks the new one ("Checking…" in
  the field, 700 ms at least so a quick answer does not flash). The console
  proves it is the same server by asking the new address who the signed-in
  account's token belongs to; only the server that issued a token accepts
  it. Same user back: the address is saved, the covers move with it, and the
  app starts again on it. Otherwise the field says "No RomM server there" or
  "A different server: use Sign out", with the typed address kept behind the
  message to correct. **Proved 2026-09-25** against a second, throwaway RomM
  (5.3.1): it refused the real token. **Only the signed-in account's token is
  tried**, deliberately: if that token is dead the console cannot load its
  library on the old address either, so this screen is not where that is
  fixed (MMagTech: tokens can be set never to expire). A server reinstalled
  from scratch reads as a different one, which is right, since its game
  numbers change. A new address for the same server never signs out.
  **Built, #60.**
- **And from the startup screen, when the server does not answer.** A
  server whose IP changed is exactly the console that never reaches
  Settings. So "Waiting for your server, 12s" gains a second, brighter line
  after 10 s: "Press (A) to change the server address". A opens the PIN pad
  if one is set, then the same keyboard and check as Settings. The same
  server at the new address: the start carries on there, no restart. A
  different server: a panel titled "A different server" with Sign out's two
  lines, focus on Cancel, because Settings is out of reach. Settings keeps its
  row for a move made while the old address still answers (a hostname,
  https) and for a console already running when the server moves. MMagTech,
  2026-09-25. **Built, #60**, judged on the TV the same day.
  - **A server that is simply off loses nothing.** It is tried every 2 s and
    the start carries on once it answers; the same address typed while it is
    down says "No RomM server there", never "a different server".
  - **It waits for as long as it takes**, with no give-up: it used to quit
    at 90 s and the session started it again, a black flash every minute
    and a half while a server was off, which bought nothing once this screen
    retried by itself. The counter goes on in minutes ("4m 10s"). MMagTech,
    2026-09-25; the (A) line is the cue to go and check the server.
- **Sign out:** one question, "Sign out?", focus on Cancel:
  "Removes every game and account from this console." then "Saves stay on
  the server.", or, if any save never reached the server, "Saves waiting to
  upload will be lost." Said, not counted (MMagTech, 2026-09-25). Nothing
  re-sends an old unsent save from the disk (the marker records only a size);
  that belongs with the offline console, open question 22. Uploads still in
  flight are waited for, 20 s at most. Then everything tied to that server is
  cleared: kept and cached games on every drive plugged in, every account's
  saves, states and keeps on this console, its covers, the accounts, their
  tokens, the PIN and the address. Wi-Fi, controllers, BIOS and the console's
  own settings stay. Then first run, at the server step, then pairing, then
  straight into the console: the network and the controllers stayed, so
  their steps and the Ready screen are skipped (MMagTech, 2026-09-25: "we
  just need the ip screen"; pairing cannot be skipped, the accounts are
  gone). **Built, #61.** On the A9, 2026-09-25: 8.52 GB cleared in 0.05 s.
  - **First run no longer freezes on "Start playing".** Its Bluetooth scan
    was joined on the way out with nothing drawn: 17 s on a frozen Ready
    screen, on every new console too. The startup screen now covers it.
  - **Crash-left Dreamcast cards** (`saves/unattributed/`) go with it and
    were never uploaded. MMagTech: fine to lose.
  - **A games drive that is not plugged in keeps its games.** They are that
    server's and nothing will use them; a wrong one is never launched,
    because a file is only reused at its game's number, name and size.
  - **Dropped: keeping games through Sign out** until the next pairing shows
    whether it is the same server. It would protect a kept library from an
    accidental sign out, but the one deliberate same-server case (a dead
    token) is exactly where the check cannot work, it leaves a credential on
    the disk after "Sign out", and the question would have to explain itself.
    An accident takes five deliberate presses, and the PIN when set.
- **Leaving is the startup screen, not black.** Sign out and a new address
  both end with the app starting again (the address is read once, and first
  run runs before the main loop). The curtain is the startup screen (the
  cabinet, the name, "Signing out" or "Connecting to <address>"), and the app
  replaces itself in place (exec), so gamescope stays up and the next picture
  is the same screen. MMagTech, 2026-09-25: *"not just flat purple, the screen
  you see with the logo."* Measured on the A9: 0.14 to 0.24 s between the
  last frame and the next.
- **One server at a time.** A friend's server is sign out, then sign in.

## Display and Sound

- **Picture** (picture quality): Performance, Balanced or Quality for the
  whole console, and a game's pause menu can override it for that game with a
  "Picture: Balanced" row (Console, Performance, Balanced, Quality; from the
  game's next start, said in the pill). Named "Picture", not "Picture
  quality", because "Picture quality: Quality" said it twice (MMagTech on the
  TV, 2026-10-02). With a PIN set, only the owner sees it. Until somebody
  moves it, it shows the level the console picked from its graphics chip at
  start (#63 phase 2); after that it is theirs. Record: PROJECT.md, the #63
  decisions of 2026-10-02 and 2026-10-04.
- **Appearance:** Standard, Dark or Scheduled, the console's, in
  `config/settings.json`. Dark turns the menus down, never the game.
  Scheduled adds one **Dark hours** row (8 PM to 7 AM to start) that opens
  the question panel: From and Until, then the hours. **Built on
  `home-and-appearance`, #129.**
- **Color:** sixteen: Purple, Blue, Teal, Green, Amber, Red, Wine, Graphite,
  Pink, Sky, Lime, Orange, and the gradients Sunset, Ocean, Aurora and Fire.
  The signed-in person's, stored with their account. Startup and first run stay
  purple. **Built on `home-and-appearance`, #75.**
- **Interface sounds:** one row changed with left and right: Off, Quiet,
  Medium, Loud, and remembered in `config/settings.json`. Left and right
  change it; Back returns to the list. Judged on the TV 2026-09-24.
  **Built, #62.**
- **Achievement sound** (#74): the chime with a RetroAchievements unlock, its
  own row with the same four levels, **not** tied to Interface sounds
  (MMagTech, 2026-10-06). Shown only to somebody signed in to
  RetroAchievements. Console-wide, in `config/settings.json`, like Interface
  sounds. Each step plays the chime at that level. **Built on
  `retroachievements`, judged on the TV 2026-10-06.**
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

- **Drives, with their space, each on its own line** (not one total: Eject is
  per drive, a total jumps when a drive is pulled, and it hides which drive is
  full): the main drive is **CabinetOS**, another internal drive is
  **Internal**, a drive that can be unplugged (USB, Thunderbolt, SD) is
  **External**; two of one kind are told apart by the drive's name. **Built.**
- **Drives found and not usable are listed**, with the reason ("Blank",
  "Isn't exFAT, NTFS or ext4", "Couldn't use this drive") and the size;
  Format on any of them whose contents the console can see, greyed
  otherwise. Without this a
  blank SSD fitted inside the PC would be invisible. **Built on `usb-drives`.**
- **Eject** for an External drive: stops a download going to it, unmounts
  every filesystem on it, checks nothing on the machine still has it mounted
  (File access's view included), then powers it off and says "Safe to unplug"
  in the pill. If anything still has it open: "Couldn't eject the external
  drive", and it is not powered off. Internal drives have no Eject. **Built on
  `usb-drives`, #67.**
- **Downloads** (was "Kept and cached games"), redesigned with MMagTech
  2026-09-25, **built 2026-09-26 on `download-row`, #68**, judged on the TV.
  - **Only downloaded (kept) games. The cache is not shown:** it clears
    itself, so there is nothing to decide, and it still counts in each
    drive's free space. His own rule, 2026-09-16: "no one knows or cares if
    the game is cached".
  - **Only games on a drive that is here.** A game on an unplugged drive takes
    no room on this console and comes back into the list with its drive; a
    system with games on both shows the ones that are here (MMagTech on the
    TV, 2026-09-26, after seeing it listed as "Drive not connected"). The
    game's own screen still says Remove download for such a game, and Play
    fetches a stand-in as before. **A known rough edge, left for users to
    report:** a drive that never comes back leaves those games marked
    downloaded on their screens (a few hundred bytes each; Remove download
    there clears it). Showing Download instead was considered and dropped:
    it would put a second copy on the main drive whenever a stick is only
    unplugged for a while.
  - **The Storage row** reads "Downloads", "10 games · 4.2 GB" under it; with
    nothing downloaded it is greyed, "None".
  - **Two levels, like the Library** (a drive full of NES games would be one
    list of hundreds): a panel with the systems, biggest first, each with its
    count and size, and **Remove all** above them; a system opens its games,
    biggest first, each with its size, and the drive's name after it when more
    than one drive is attached. Named as the Library names systems ("Arcade
    (FinalBurn Neo)").
  - **Inside a system, A ticks.** **Select all** (Select none once all are
    ticked) and **Remove** are pinned above the list; Remove is greyed until
    something is ticked, then "Remove 3 games · 1.2 GB". **No single press
    removes anything:** every removal is one question naming the game, or how
    many, and the size, with Cancel focused. A system opens on Select all
    (it only ticks); the systems list opens on the first system, not on
    Remove all. No ticking on the systems list (A opens there), and ticks do
    not carry between screens. Left and right jump a page in a long list.
  - **A and B only**: the whole console runs on the D-pad, A and B, and a
    keyboard works everywhere; a Select button would be the first break.
  - **No names. Removing takes the game for everyone** who downloaded it:
    the space belongs to the machine. MMagTech: a parent who minds sets the
    PIN. **Opening Downloads asks the PIN** if one is set: it is an admin
    screen. Once per Settings visit, like every PIN in Settings, so clearing
    several games is one PIN. A person without the PIN still removes their
    own download from the game's own screen.
  - After a removal the pill says "Download removed" or "3 downloads
    removed", the list shrinks, an emptied system goes back to the systems,
    and an empty list closes.
  - In Storage, not a category of its own: it is where "Storage almost full"
    sends a person looking.
  - Seven rows at most, so the panel clears the top bar.
- **Not enough space, said short with what to do** (MMagTech, 2026-09-26:
  the old messages were too long). Play: "Not enough space. Remove some
  downloads". Download: "Needs 3.2 GB more. Remove downloads or add a
  drive". Only Download mentions a drive, because only a Download spills
  onto one; Play always fetches onto the console's own drive.
- **The game screen's row says Download**, not "Download and keep"
  (MMagTech, 2026-09-26): one set of words, Download, Remove download,
  Downloads. "And keep" only told it apart from Play's fetch into the cache,
  which nobody sees.
- **File access** (under Storage): **built on branch `file-access`, #69;
  screens judged on the TV 2026-09-25, the login still to be tested.**
  - **One row**, "File access", "SFTP" under it, On or Off. **Everything a
    computer needs is in a panel the row opens**, not in rows under it:
    as rows they ran off the bottom of Storage, the same reason Wi-Fi's
    networks are in a panel (MMagTech on the TV, 2026-09-25). The panel:
    the address and `<hostname>.local`, "User name  cabinet", "Password
    XXXX-XXXX", then **Done** (focused), **New password**, **Turn off**.
    Judged readable from the sofa as it is.
  - Turning it on asks for the PIN when one is set, then the panel opens by
    itself once it is on. **Opening the panel again, while on, asks for the
    PIN too** when one is set (once per visit): the password is in it
    (MMagTech, 2026-10-07). **Turning it off never asks**: closing a door
    needs no key. **New password** asks the PIN, then "New password?" with
    focus on Cancel (every computer that saved the old one breaks), then
    the panel comes back with the new one. A failure shows in the row:
    Off, with the reason in place of "SFTP".
  - SFTP only, no shell. Off until turned on; on is remembered, so a
    console left with it on turns it back on at boot.
  - User name `cabinet`, fixed.
  - Password made by the console the first time it is turned on, kept
    through restarts and off and on, shown in plain text in the panel,
    changed only by **New password**. **8 characters, `XXXX-XXXX`**,
    RomM's pairing code's shape, from letters and digits that cannot be
    confused on a television (no 0/O, 1/I/L). It is `cabinet`'s login
    password, because that is what sshd checks, so on today's images it is
    also the sudo password; that goes when the session user leaves `wheel`.
  - From a computer you see `roms`, `cache`, `bios` and `users` (the
    saves), and one folder per extra drive named as Storage names it
    ("External", "Internal") holding only that drive's `CabinetOS` folder.
    `config` (the RomM token) and `logs` are not there. Bind mounts in
    `/run/cabinetos-files`, the chroot; taking them down never deletes
    (rmdir only). A drive plugged in while it is on appears the next time
    it is turned on.
  - **`cabinetos.local` already works**: Bazzite ships avahi. Open question
    9's mDNS half is answered by the base.
  - **Ports, MMagTech 2026-09-25:** port 22 is File access (password, SFTP,
    chrooted, listening only while on); **the development shell moved to
    port 2222**, then key only and always on, on development images only
    (`cabinetos-dev-ssh.service`). **Since 2026-10-07 port 2222 is Developer
    access** (System, below): off until switched on, the same password or a
    key. sshd cannot tell a key login from a
    password login in a Match block, so the port is the line between the
    two uses of one account. SELinux allows sshd only port 22, so 2222 is
    labelled at boot by the unit itself (a label in the image would not
    reach a console with any local SELinux change). Keyboard-interactive is
    off on 2222 too; it is a second way to type a password.
  - Setting your own password: **later**, if people ask.
- **Extra drives, decided with MMagTech 2026-09-25, built on `usb-drives`:**
  - **Mounted by the console itself**, internal and external alike, when it
    starts and when one appears (udisks2, `frontend/src/drives.cpp`). Found
    2026-09-25: nothing mounted a USB drive, because the desktop's automounter
    went with the desktop. Never the console's own drive, never Windows' EFI,
    reserved or recovery partitions.
  - **exFAT, NTFS and ext4** (ext4 added 2026-10-04, #236: only it holds
    Steam's Windows games properly, so one drive can hold both). Anything
    else, and a blank drive: "isn't exFAT, NTFS or ext4". An ext4 drive made
    elsewhere gets an empty `CabinetOS/` from a root helper
    (`cabinetos-drive-claim`), since its top folder is root's.
  - **The console is the only thing that mounts a drive**: Bazzite's
    automounters are removed from the image (#236, docs/PROJECT.md).
  - **Kept games go on the main drive first**, up to 80% of it (not counting
    the cache, which clears itself), then to the extra drive with the most
    room; with none, the main drive after all, down to the floors. Replaces
    "the first extra drive whenever one is plugged in", which sent games to a
    small stick while a big main drive sat empty. Nothing already on a drive
    moves. Cached games and saves stay on the main drive, as before.
  - **Offline (#90, not built):** a game kept on a drive that is not attached
    cannot be played and shows greyed. Online it downloads a stand-in, as
    today. Saves never leave the main drive, so nothing of a person's progress
    goes with the drive.
  - **Notices, in the pill, no sound** (a chime would play over a game):
    "External drive connected" once mounted and usable, "Safe to unplug"
    after Eject, "External drive removed" when pulled without it, "External
    drive isn't exFAT, NTFS or ext4", "Couldn't use the external drive". A drive
    attached before the console started is mounted without "connected".
    **Internal drives get no notices** (a bad one would say so every boot);
    they are listed greyed in Storage instead.
  - **"Storage almost full"** after a Download that leaves under 10% free
    across all drives (counting the cache as free). The one early warning:
    only a Download can fill the drives.
  - **The drive missing at boot is not said on screen** (it only ever went to
    the log): the pill covers pulling it while on, and a drive unplugged
    while off was unplugged by the person who knows it.
  - **File access** shows each drive as a folder named as Storage names it,
    and rebuilds the folders when a drive comes or goes. Before this, a
    second External drive never appeared, and one plugged in while File
    access was on only appeared after turning it off and on.
  - **Format, on any drive with no file under `CabinetOS/`** (2026-10-04,
    #236, MMagTech: "I got this new drive, and I want to just plug it into
    my CabinetOS and go"). Replaces "a blank drive only", below. An External
    drive in use asks "Eject / Format / Cancel" (Eject first: it is what the
    row says); an Internal one in use, a blank one and one in another format
    say Format on their row. The confirm says what is on the drive ("Holds 3
    files, 12 GB", "Empty", "and 1 partition in another format"), counted
    when asked. **While it formats, a screen of its own** ("Formatting
    External", the drive, "Don't unplug the drive", a spinner; Back does
    nothing) stays up until the new drive is mounted, then the pill says
    "External drive formatted" (MMagTech on the TV, 2026-10-04: the drive
    left Storage and nothing said a format was running; the SanDisk stick
    takes about two minutes, its small writes being that slow; exFAT wrote
    far less and took 5 s). It makes one **ext4** partition named "Games" with
    `CabinetOS/` and `SteamLibrary/`; Steam is told about `SteamLibrary/` at
    each handover. Never the console's own drive, one mounted by anything
    but the console, or one with a filesystem it reads but could not mount.
  - *Was:* **Format, for a BLANK drive only** (no partition table, no filesystem:
    a new SSD). MMagTech first decided the console never formats (a
    formatting bug is a wiped drive), then raised the case that breaks it the
    same evening: a blank SSD fitted inside the PC cannot be formatted
    anywhere else. A blank drive is listed as "Blank"; pressing it asks the PIN if set, then "Format this drive?" naming the
    drive, its model and size with **Cancel** focused (so a drive wrongly
    read as blank is recognised), then a **random 4-digit code** on the PIN
    pad (his idea; typing "format" with a pad was rejected as slower and no
    safer). It makes one exFAT partition named "Games" ("Games 2" and on if
    one is attached: two formatted drives must be told apart; exFAT names
    are 11 characters), and the drive is
    then used like any other ("External drive connected"). Blankness is
    checked again from udisks right before writing. Built on `usb-drives`;
    PROVED on the A9 2026-09-25 with the installer stick wiped blank
    (MMagTech's permission): code, GPT, exFAT, mounted, claimed, in 5 s.
    The internal case is for the VM's virtual disks.
  - **The drive's own row is the button, and says what it does** (MMagTech
    on the TV, 2026-09-25: a Format row under the drive "can make me feel
    like I'm formatting something that isn't the unformatted drive", and
    then, with a chevron alone, "I have no way of knowing clicking it allows
    it to be ejected"). Each drive: its name, the space on the second line,
    and its action as the value, **Eject ›** or **Format ›**. Eject asks
    "Eject / Cancel" titled with the drive's name. A drive with nothing to
    do shows no value. With two of a kind, a drive in use is named by its
    label, "External (T9)", and one not in use by its model, "External
    (SanDisk 3.2 Gen1)".
  - **Sizes: two decimals from 1 TB up** ("2.02 TB free of 2.05 TB"), whole
    GB below. With one decimal the A9's main drive, 23 GB used, read "2.0 TB
    free of 2.0 TB". Decimal units, as the box, the Mac and the PS5 count.

- **Steam** (#223; PROJECT.md open question 37): **only once Steam is set
  up**, a line under the main drive with the size of Steam's slice; the main
  drive's own line leaves that slice out. Pressing it (PIN if set, once per
  visit) offers **Adjust storage**, which only grows it, 25 GB a press,
  clearing cached games and never kept ones, and **Remove Steam**, "Remove
  Steam?" with Cancel focused, which gives the whole slice back. **Built on
  `steam-handoff`.**

## System

- **System update.** Decided with MMagTech, 2026-09-25; **built on the
  `system-update` branch, #70, being judged on the TV** (the row's states
  and the panel were judged and passed on 2026-09-25; the real update is
  next). Why it comes
  first: the image switches off every automatic updater on purpose
  (`strip-desktop.sh`: `uupd`, `bootc-fetch-apply-updates`), so today a
  console only updates if someone runs `bootc upgrade` over SSH.
  - **Check for updates:** a row, **Manual** (the default) or **Weekly**.
    Weekly only ever checks, never downloads. It runs when all of these hold:
    on Home, no game running, online, and more than 7 days since the last
    successful check. So offline skips it, and the next boot or reconnection
    catches up with no separate trigger. An automatic check that finds
    nothing says nothing; one that finds an update shows the pill "Update
    available" once per new version.
  - **The System update row:** the value on the right and a line under it.
    **A console that has never checked shows the row alone**, no value and
    no line; pressing it checks (MMagTech on the TV, 2026-09-25: no state
    for "never checked").
    **A check somebody pressed that finds an update opens a panel**,
    "Update available" / "2026.09.28 · 0.9 MB" / **Download** · Later, and
    the row opens it again; Download goes to the PIN. Without it the second
    press that downloads was nowhere on screen (MMagTech on the TV,
    2026-09-25). A weekly check stays quiet: the pill only.
    Up to date / "Checked today" (both kinds of check write that line;
    "yesterday", "3 days ago" by the calendar).
    Checking…. Update available / "2026.09.28 · 0.9 MB": the size is shown,
    because a base-image update is hundreds of MB to GB. Downloading: "45%" /
    "0.4 of 0.9 MB". Installing… with the time taken and the amount written
    ("3m 12s, 1.4 GB written"), because unpacking has no honest percentage
    and a spinner would not show it is not frozen. Restart to update.
    Couldn't update / "Stalled" or the reason; pressing retries. **A failed
    check says "Couldn't check"** / the reason ("No connection"), because it
    was not an update that failed.
  - **PIN:** starting the download asks for it when one is set. Checking,
    the status, Manual or Weekly, and Restart now or Later do not. MMagTech
    reversed "system update open to everyone" (2026-09-24): an update can be
    gigabytes and replaces the system, so the owner decides when.
  - **When it applies:** asked once the download is ready, in a panel
    "Update ready": **Restart now** / **Later**. Later applies it at the next
    Restart or Power off (bootc's staged deployment); Sleep does not, and the
    row says "Restart to update" until then, with the version under it, and
    pressing the row asks again. Restart now waits for saves still
    uploading, as Sign out does: **it is the Power menu's Restart**, whose
    logind delay lock already holds the machine up to 25 s for uploads
    (PR #52), so there is no second waiting mechanism. The panel's line is
    the version.
  - **Never during a game.** No check and no restart while an emulator runs.
    A download started before a game carries on at low priority (the
    download always runs at low priority: `Nice=15`, I/O best-effort 7;
    in a menu nobody can see the difference); if it
    finishes during the game, nothing appears until the game is closed, then
    the "Update ready" panel shows once on Home.
  - **Stalls:** the root service watches the updating processes' CPU time,
    bytes read (which includes the network) and bytes written. Stalled when
    none has moved for 60 s. A slow connection keeps moving and is allowed;
    there is no overall limit. On a stall the service stops the update, the
    row says "Couldn't update" / "Stalled", and pressing retries; no
    automatic retries. The running system is untouched until the new one is
    complete, so a stall or power cut leaves the console as it was. A retry
    should reuse what already arrived: **to verify** by pulling the cable
    mid-download.
  - **Afterwards:** on the first start after an update, the booted version is
    compared with the one installed. Match: the pill "Updated to
    2026.09.28". Not: "Update didn't apply".
  - **Root:** the session runs as `cabinet`, so the image carries
    `/usr/libexec/cabinetos-update` (`check`, `download`), run by two oneshot
    units, `cabinetos-update-check.service` and
    `cabinetos-update-download.service`, and a polkit rule,
    `61-cabinetos-update.rules`, that lets `cabinet` at the console (not over
    SSH) START those two units and nothing else. The answer is one file,
    `/run/cabinetos-update/status`, key=value lines, which the frontend reads
    twice a second. **Proved from the TV on 2026-09-25**: pressed, started,
    checked as root, answered, in 0.75 s.
  - **How the pieces map onto bootc** (1.16 on the A9): `bootc upgrade
    --check` says whether there is an update and its version. The **size**
    is worked out by the script: the new manifest's layers minus the layers
    ostree already holds (one ref per layer), which is exactly what bootc
    fetches; its own "layers needed: 3 (60.2 MB)" matched to the byte.
    **Downloading** is bootc's JSON progress (`--progress-fd`, which has to
    be a pipe), bytes of bytes. **Installing** is everything after the last
    layer: bootc reports only four uneven steps there, with no bytes and no
    total, which is why it shows time and bytes written. **To measure on the
    first real update:** bootc unpacks each layer as it downloads, so
    Installing may last only seconds, in which case its line is barely worth
    having.
  - **Weekly, and "online":** there is no separate online test. A weekly
    check that fails says nothing and may try again half an hour later,
    which is how "offline skips it" and "the next reconnection catches up"
    both happen.
  - **Emulator Flatpaks** are pinned by the image and fetched by a service
    at boot, so an update that moves one downloads part of itself after the
    restart, outside the size shown. Rare; already handled.
- **The version is the date,** `2026.09.28`, and `2026.09.28.2`, `.3` for
  further images that day; UTC. The build stamps it into the image (the label
  the update check reads, and a file the console reads after booting), and
  numbers a day by the versions already in the registry, so a docs-only
  commit that builds nothing does not use a number up. This is also About's
  version (#72). MMagTech, 2026-09-25. **Built:** `ci/next-version.sh`
  reads the registry's tags; every published image is also tagged with its
  version, which is the record. `latest` and `testing` share one sequence,
  so a version names one image. The file is `/usr/share/cabinetos/version`,
  in a small layer of its own, last, so it drags nothing else into an
  update. Two builds overlapping on one day could pick the same number; the
  push refuses a version already published, and a re-run takes the next.
  The first: `2026.09.25`, on `testing`.
- **A `testing` tag.** Builds from a `testing` branch publish to
  `cabinetos:testing`, never `latest`; MMagTech's test console follows it
  (switched over SSH with `bootc switch`), and its update check then follows
  `testing` by itself. Every user's console follows `latest`, and nothing on
  screen changes that. Set up first, to test updates on. **Later:** a "Test
  builds" switch in developer settings for people who want to help test.
  **Built 2026-09-25**: `testing` publishes `testing`, `testing-<sha>` and
  its version, never `latest` or the date tags. Push work to it with
  `git push origin <branch>:testing`. **A push that brings no new commits
  builds nothing** (creating `testing` at a commit GitHub already had from
  another branch): the trigger's `paths` filter sees no changed files. Start that one by
  hand, `gh workflow run build.yml --ref testing`. The A9 was switched to it
  on 2026-09-25 (a 60.2 MB download), to go back to `latest` when System
  update is done.
  **THE TESTED IMAGE IS WHAT SHIPS, 2026-09-25.** MMagTech: *"The intent of
  test was I could build and test features and then when all was good we
  go to main."* So a merge to main does not build: if main's files are
  exactly those `testing` was built from (the git tree, so a merge commit
  from an up-to-date branch counts), that image is tagged `latest` in
  seconds, same digest, same version (`ci/promote-tested.sh`). Documentation
  does not count (#156): `.md` files and `docs/` are not in the image, so a
  handover written after the testing push still promotes; `docs/LICENCES.md`
  is in the image and does count, and a change to it alone builds. Otherwise it
  builds, as before. **The rule: push the final commit of a branch to
  `testing`, judge it, then merge.** Pull requests no longer build the whole
  image; they lint and compile the frontend. Until that day `testing` was a
  separate channel and main rebuilt everything, so a feature was built
  three times and `latest` was a rebuild of what had been judged.

- **Steam · Hidden**, only while somebody has chosen Hide Steam on Steam's
  first screen (#223): pressing it (PIN if set) puts "Switch to Steam" back in
  the Start menu and the row goes. Odd company for System update, and the
  least odd place (MMagTech, 2026-10-03). **Built on `steam-handoff`.**

- **Developer access** (#134; decided with MMagTech 2026-10-07, built on
  `developer-access`): **one row**, "Developer access", "SSH" under it, On
  or Off, **in About, under Version, and hidden until Version is pressed
  seven times** (each within two seconds of the last), the way Android hides
  its developer options. Seven more hide it again, **and hiding turns it
  off**: nothing listens behind a switch nobody can see. A console with it on
  always shows it. Why hidden: families never see it, and on a console with
  no PIN a child on the owner's account cannot find it to choose one; the
  PIN and the password are still what protect it (MMagTech, 2026-10-07: "if
  you can show it you should also be able to hide it again"). Why About: the
  row appears where it was unlocked, and About is the least visited screen. The full command line over SSH, port
  2222, user `cabinet`: "when they turn it on, they now are a developer.
  Same install." **One image for everyone**: there is no development image
  any more, and nothing listens on 2222 until this is turned on.
  - **Off by default, and off means `cabinetos-developer.service` is not
    running**: the port is closed, not filtered. Turning it off also ends
    every session it started. On is remembered through restarts, as File
    access is, until it is turned off.
  - **The password is File access's** (one password, shown in one place;
    MMagTech: the people using this are technical, and turn each off when
    done). A key works too. The panel opens by itself once it is on: the
    address and `<hostname>.local`, "Port 2222", "User name cabinet",
    "Password XXXX-XXXX", then **Done**, **New password**, **Turn off**.
    **New password** changes File access's too. Opening the panel again asks
    for the PIN, once per visit.
  - **IT NEEDS A PIN TO EXIST** (MMagTech, 2026-10-07). On a console with no
    PIN, turning it on first asks the owner to choose one; another account
    is told "Needs a PIN from <owner>". The PIN is what a Linux install's
    admin password would be: it lets somebody at the TV turn it on and see
    the password, and never leaves the console; the network still needs the
    password. **Not File access**, which keeps "the PIN when one is set": the
    PIN is one lock for the whole console, and requiring it there would put a
    family that only copies saves behind PIN prompts for Wi-Fi, accounts and
    updates too.
  - **After a fresh install** a console has no key on it: turn this on and
    copy a key over once with the password,
    `ssh-copy-id -p 2222 cabinet@<hostname>.local`.
  - `cabinet` is in `wheel`, so the password is also sudo's: full access is
    what this switch is for.

## About

- **Version:** the date version (see System), with Bazzite's underneath,
  "Bazzite 44.20260916". A console that does not follow `latest` adds the
  tag, "2026.09.25.2 · Testing", so a test console is told from the rest at
  a glance; nothing on screen changes the channel. Not focusable: a fact,
  not a control. Read without root: the version file, Bazzite's
  `/usr/share/ublue-os/image-info.json`, and the booted deployment's
  `.origin`. **Built, #72.** MMagTech, 2026-09-25.
- **Credits and licences: ONE row, not two.** A Credits list and a
  Licences list would name the same projects twice, so it is one list, one
  line per project: its name, what it does, its licence ("Snes9x · SNES ·
  Non-commercial"). Ours, the base (Bazzite, Universal Blue, Fedora),
  gamescope, the emulators with the six non-commercial ones first, then the
  libraries and the type. The facts are `docs/LICENCES.md`'s; the list is
  `kCredits` in main.cpp. **No full licence texts on the television**: they
  are in the image under `/usr/share/licenses/`. **gamescope is credited and
  ChimeraOS is not**: nothing of ChimeraOS's is used directly (our session
  is our own script running Valve's gamescope). MMagTech, 2026-09-25.
  **Built; being judged on the TV.**

## In the pause menu, per system (not Settings)

Every option Cabinet offers is kept, only moved here, because the picture is
right there and the choice is about a system:

- **Virtual Boy:** 3D glasses (Off, red and blue, red and cyan, red and
  electric cyan, green and magenta, yellow and blue) and all eight screen
  colours. MMagTech's condition: every one of Cabinet's choices.
- **Game Boy colours** (Off, Auto, Game Boy Color, Super Game Boy), for
  original Game Boy games only: Gambatte ignores it for a Game Boy Color game.
- **Virtual Boy's two are ONE ROW, "Screen"** (MMagTech on the TV,
  2026-10-02): the eight colours, Red first and by default, then the five 3D
  pairs ("3D red/blue"). With glasses on the core draws in the glasses' two
  colours and ignores the screen colour, so two rows let a person set a
  colour that did nothing, and made the menu seven buttons long.
- **Narrowed with MMagTech 2026-10-02**, to two rows on two systems:
  - **GBA screen colours: dropped.** Cabinet's own notes say it shifted hues
    wrongly on device and left it off pending a fix.
  - **GBA blending: no row, fixed on Smart** (`mix_smart`, blends only pixels
    that flicker), and **Atari 2600 flicker blending: no row, fixed on
    Medium ghosting** (`ghost_75`; Average still strobed Ms. Pac-Man's
    ghosts and Heavy trailed, judged on the TV 2026-10-02). Each checked on a game known to flicker before it ships.
  - **Vectrex overlays: dropped for now.** Not a core setting: Cabinet draws
    each cartridge's sheet from its own images, none of which is built here.
    Its own issue, after the first release.
- **How the rows behave:** centred like every other button, "3D glasses: Off",
  with an arrow at each edge when selected; they change instantly, in front
  of the game.
- ~~Controller type: Mega Drive 3 or 6 button, PC Engine 2 or 6 button.~~
  **Dropped, MMagTech 2026-10-02.** Mega Drive: Genesis Plus GX picks 3 or 6
  buttons per game itself when handed a plain pad, as this console does (read
  in source, not tested). PC Engine: stays on 2 buttons, its default; one
  switch per system would be wrong for some games either way, and choosing
  per game would need a list of games, which the no-per-game-fixes rule rules
  out. Cabinet's option is not carried over.
- ~~Shader and glow, as already decided.~~ **Reopened and decided
  2026-10-04 (#122):** one Look row per system, RetroArch's own shaders,
  on by default; the glow has no row and is always on at Strong. Both in
  docs/PROJECT.md, *Shaders, and the glow around the picture*.

**To build, #73.**
