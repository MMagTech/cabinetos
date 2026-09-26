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

- **Drives, with their space, each on its own line** (not one total: Eject is
  per drive, a total jumps when a drive is pulled, and it hides which drive is
  full): the main drive is **CabinetOS**, another internal drive is
  **Internal**, a drive that can be unplugged (USB, Thunderbolt, SD) is
  **External**; two of one kind are told apart by the drive's name. **Built.**
- **Drives found and not usable are listed greyed**, with the reason ("Isn't
  exFAT or NTFS", "Couldn't use this drive") and the size. Without this a
  blank SSD fitted inside the PC would be invisible. **Built on `usb-drives`.**
- **Eject** for an External drive: stops a download going to it, unmounts
  every filesystem on it, checks nothing on the machine still has it mounted
  (File access's view included), then powers it off and says "Safe to unplug"
  in the pill. If anything still has it open: "Couldn't eject the external
  drive", and it is not powered off. Internal drives have no Eject. **Built on
  `usb-drives`, #67.**
- **Kept and cached games:** see what is on the console, keep or release.
  **To build, #68.**
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
    itself once it is on. **Turning it off never asks**: closing a door
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
    port 2222**, key only, on development images only
    (`cabinetos-dev-ssh.service`). sshd cannot tell a key login from a
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
  - **exFAT and NTFS only.** Anything else, and a blank drive: "isn't exFAT
    or NTFS". Users are technical and made the installer on a computer that
    can format a drive.
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
    drive isn't exFAT or NTFS", "Couldn't use the external drive". A drive
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
  - **Formatting: open.** Decided 2026-09-25 that the console never formats;
    then, the same evening, MMagTech raised the case that breaks it: a blank
    SSD fitted inside the PC, which cannot be formatted anywhere else. The
    proposal: Format offered only on a completely blank drive (no partition
    table, no filesystem), after the PIN, a confirm naming the drive and its
    size with focus on Cancel, and a random 4-digit code on the keypad.
    Tested on a USB stick wiped blank. Not built.

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
  another branch): `paths-ignore` sees no changed files. Start that one by
  hand, `gh workflow run build.yml --ref testing`. The A9 was switched to it
  on 2026-09-25 (a 60.2 MB download), to go back to `latest` when System
  update is done.
  **THE TESTED IMAGE IS WHAT SHIPS, 2026-09-25.** MMagTech: *"The intent of
  test was I could build and test features and then when all was good we
  go to main."* So a merge to main does not build: if main's files are
  exactly those `testing` was built from (the git tree, so a merge commit
  from an up-to-date branch counts), that image is tagged `latest` in
  seconds, same digest, same version (`ci/promote-tested.sh`). Otherwise it
  builds, as before. **The rule: push the final commit of a branch to
  `testing`, judge it, then merge.** Pull requests no longer build the whole
  image; they lint and compile the frontend. Until that day `testing` was a
  separate channel and main rebuilt everything, so a feature was built
  three times and `latest` was a rebuild of what had been judged.

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
- Game Boy colours, GBA screen colours and blending, Atari 2600 flicker
  blending, Vectrex overlays.
- Controller type: Mega Drive 3 or 6 button, PC Engine 2 or 6 button.
- Shader and glow, as already decided.

**To build, #73.**
