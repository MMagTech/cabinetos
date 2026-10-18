# What's new

Every update that reached consoles, newest first. To update: Settings,
**System**, **System update**. An update you skipped is included in the next
one you install, so you only ever install the newest.

## 2026.10.18.6 {#v2026-10-18-6}

*18 October 2026*

- **New:** PS1 games get a DualShock. The sticks work in the games made for
  them, and the controller rumbles. Games made for the original pad play as
  they always did. A game that needs the DualShock's ANALOG button pressed
  takes **L1 + R1 + Select**.
  ([#345](https://github.com/MMagTech/cabinetos/issues/345))
- **Fixed:** the Nintendo DS touch screen did nothing. The right stick now
  moves a pointer on it, and pressing the stick (**R3**) touches.
  ([#374](https://github.com/MMagTech/cabinetos/issues/374))
- **New:** Nintendo DS games draw their 3D sharper at the Balanced and
  Quality picture levels, 4 and 6 times the DS's own size. Performance keeps
  the DS's own size, as before.
  ([#219](https://github.com/MMagTech/cabinetos/issues/219))

## 2026.10.18.5 {#v2026-10-18-5}

*18 October 2026*

- **Fixed:** with more than one PS2 BIOS on your server, every game started
  on whichever BIOS the emulator found first. Each game now starts on a BIOS
  for its own region (USA, Europe or Japan), going by the disc code RomM
  reads from the game, whatever the files are called. A game with no BIOS
  for its region uses your USA one.
  ([#416](https://github.com/MMagTech/cabinetos/issues/416))

## 2026.10.18.4 {#v2026-10-18-4}

*18 October 2026*

- **Changed:** the system CabinetOS is built on moves to Bazzite
  44.20261012.1, with Linux 7.2.9. The new kernel has fixes for Radeon
  graphics, Bluetooth and sound, and the libraries that draw the screen,
  download games from RomM and run the Original Xbox and Switch emulators
  are newer versions.
  ([#503](https://github.com/MMagTech/cabinetos/pull/503))

## 2026.10.18.3 {#v2026-10-18-3}

*18 October 2026*

- **Fixed:** a game kept on your RomM server as one file inside its own
  folder now starts. RomM makes such a folder when you upload a manual, a
  soundtrack or screenshots to a game. The console used to hand the
  emulator the soundtrack or the manual instead of the game, and a PS2 game
  alone in its folder showed the red "not a PlayStation or PlayStation 2
  format disc" screen with every BIOS. Now only the game file downloads.
  ([#400](https://github.com/MMagTech/cabinetos/issues/400),
  [#416](https://github.com/MMagTech/cabinetos/issues/416))

## 2026.10.18.2 {#v2026-10-18-2}

*18 October 2026*

- **Fixed:** a console set up over Wi-Fi now gets its Xbox and Switch
  emulators without needing a restart.
- **Fixed:** a game no longer opens on an old save state, or on the wrong
  disc, after a game before it failed to start, or after resuming a Switch,
  PS3 or Xbox game from Home.
- **Fixed:** Resume on Home now says why a game cannot start (for example
  "Not enough space") instead of doing nothing.
- **Fixed:** on a nearly full console, Play no longer deletes the game it is
  about to download and then fail with "Couldn't download this game".
- **Fixed:** in PS3, Switch, Xbox, Xbox 360 and Wii U games, a controller
  switching off now pauses the game and the other players keep their
  numbers; a button held when the game started no longer stays pressed on
  Home afterwards; a message showing when you close the game's pause menu
  no longer stays frozen on the game; and a stray "Paused" box no longer
  shows over a game while A is held.
- **Fixed:** an Xbox game's saves can no longer be lost when they could not
  be copied off its drive the last time it was played.
- **Fixed:** with a USB keyboard, holding an arrow now speeds up on every
  screen, and holding Return or Escape acts once (holding Return on Exit to
  Home no longer starts the game again).
- **Fixed:** downloaded Wii U games now show on Home, the Library and Search
  while the console is offline.
- **Fixed:** a switch to another person that fails now goes back to whoever
  was signed in, instead of mixing the two people's games, saves and RomM
  sign-in.
- **Fixed:** signing out now also forgets each game's picture choices and
  what the console read off its files, so a new server's games never take
  over another server's.
- **Fixed:** in Settings, B on a question goes back to the panel it came
  from, as Cancel does; coming back to Add an account while its code is
  still valid shows that code again; the cursor stays on the controller you
  just gave a new player number; and the Wiki's QR code in How to no longer
  jumps smaller as you move away from it.
- **Fixed:** Home keeps saying "Nothing played yet" until you play
  something, instead of filling Recent with every game you browsed; a Home
  with only favourites starts on them; and games that cannot be played here
  are greyed on Home as they are in a system's games.
- **Fixed:** the Wii games turn bright or greyed the next time you open them
  after you pair or remove a Wii Remote, or a stream starts or ends.
- **Fixed:** on Search, Down from the results brings the keyboard back with
  what you typed, after opening a game or pressing Done.
- **Fixed:** first setup no longer freezes when you press a Wi-Fi network or
  a controller again while it is still joining or pairing, and a pairing
  code that expires or is refused on the phone is replaced by a new one by
  itself.
- **Fixed:** a console whose RomM sign-in has been refused, or whose RomM
  has no systems it plays, now starts to an empty Home and says so, instead
  of restarting over and over with a black screen.
- **Changed:** the grey example in the RomM server address field is now a
  made-up address.
  ([#398](https://github.com/MMagTech/cabinetos/issues/398))

## 2026.10.17.3 {#v2026-10-17-3}

*17 October 2026*

- **Changed:** nothing you should see. The console's code has been
  reorganised into smaller files named for what they do, so others can find
  their way around it and help. Every screen, game and setting works as
  before.
  ([#398](https://github.com/MMagTech/cabinetos/issues/398))
- **Fixed:** a rare fault where, while the console reconnected to your RomM
  server or changed accounts, a cover, a save upload or another request
  could go out with a half-changed server address or sign-in and fail. The
  console now changes them in one step.

## 2026.10.14.5 {#v2026-10-14-5}

*14 October 2026*

- **Added:** when two games on one system have the same name, the heading
  now shows what tells them apart, such as the region, language or revision
  in brackets in their file names on your RomM server: "Metroid Prime
  Hunters (USA) (Rev 1)". It shows in a system's games, collections, search,
  and on Home when both copies are on the same shelf. Games with plain file
  names show their name alone, as before.
- **Added:** the diagnostic report lists the games that share a name, with
  their file names, to help sort out a library where two games look the same.

## 2026.10.14.4 {#v2026-10-14-4}

*14 October 2026*

- **Changed:** the interface sounds are softer and lower, to suit the
  console's quiet look.
- **Fixed:** some places made no sound when you moved or chose something:
  a game's page, opening a game, the Resume card, the power menu on Home,
  the account switcher, the Library's tabs, the on-screen keyboard and first
  setup. They now sound like everywhere else, at the level set in
  Settings. The pause menu stays silent.
  ([#389](https://github.com/MMagTech/cabinetos/issues/389))
- **Changed:** Search no longer shows "Search" twice above the keyboard, and
  its box says "Search…".

## 2026.10.14.3 {#v2026-10-14-3}

*14 October 2026*

- **Fixed:** on some machines the console made no sound at all, because it
  played into a headphone jack or USB sound device with nothing plugged in.
  The sound now always plays on your TV. USB sound devices no longer take it
  from the TV.
  ([#341](https://github.com/MMagTech/cabinetos/issues/341))
- **Changed:** on machines whose headphone jack can tell when headphones are
  plugged in, plugging them in moves the sound to them, and unplugging brings
  it back to the TV. After a Remote Play session this needs a restart of the
  console first ([#390](https://github.com/MMagTech/cabinetos/issues/390)).

## 2026.10.14.2 {#v2026-10-14-2}

*14 October 2026*

- **Changed:** in a platform's game grid, the heading (the platform, how
  many games, and the name of the game you are on) now stays at the top
  while the covers scroll underneath, so you can always see which game is
  selected. The grid moves a whole row at a time, so no row is left half
  cut off under the heading.

## 2026.10.14 {#v2026-10-14}

*14 October 2026*

- **Fixed:** some games showed a blank tile on Home, the shelves and the
  grid, and some showed no cover even on their own page, although your RomM
  server had one. Covers now show in every picture format RomM takes (WebP,
  AVIF, GIF, BMP and TIFF as well as PNG and JPEG), and when a game's small
  cover is missing or broken, its large one is used instead. If a cover
  still does not show, a diagnostic report now says why.
  ([#376](https://github.com/MMagTech/cabinetos/issues/376))

## 2026.10.13.5 {#v2026-10-13-5}

*13 October 2026*

- **Fixed:** a console switched on while the TV was off, on another input or
  unplugged could come up at a lower size and stay blurry on a 4K TV until it
  was restarted. It now comes up at your TV's own size straight away, and
  picks the TV up, picture and sound, as soon as it is on.
  ([#268](https://github.com/MMagTech/cabinetos/issues/268))
- **Changed:** File access, Developer access and Tailscale now start when the
  console starts, if they are on in Settings, so the console can be reached
  even before the TV is on. Remote Play starts as soon as the TV is on; it
  needs a screen to stream.

## 2026.10.12.9 {#v2026-10-12-9}

*12 October 2026*

- **Fixed:** a game kept as a folder on your RomM server (for example a
  Switch game with `update` and `dlc` folders beside it, or a PS1 game as a
  `.cue` and its tracks) downloaded again every time you started it. It now
  downloads once. The first time you start such a game after updating,
  nothing downloads, and the extra copies left on your drive by the old
  downloads are cleared away.
- Updates and DLC kept beside a game are not used yet; only the game itself
  is downloaded. ([#181](https://github.com/MMagTech/cabinetos/issues/181))

## 2026.10.12.8 {#v2026-10-12-8}

*12 October 2026*

- **New:** a **Widescreen** row in the pause menu for PS2 games that play in
  16:9. Some of those games stretch their menus and on-screen display to fit;
  turn it **Off** to play that game in 4:3 as it was made. It is kept for that
  game, and shows only on games that have a widescreen option.
  ([#368](https://github.com/MMagTech/cabinetos/issues/368))
- **Changed:** when you change **Picture** or **Widescreen** in the pause
  menu, the notice now says **Exit and relaunch to apply**.

## 2026.10.12.7 {#v2026-10-12-7}

*12 October 2026*

- **New:** read a game's manual on the TV. When your RomM server has a
  manual for a game, a small book shows on its cover. On the game's page,
  **R1** opens it. **Left** and **Right** turn the page; **A** zooms in, the
  d-pad then moves around the page, and **A** again zooms out. **L1** or **B**
  goes back. Manuals are kept with the game, so they can be read offline too.
  See [Buttons and shortcuts](navigation.md).
  ([#364](https://github.com/MMagTech/cabinetos/issues/364))

## 2026.10.12.5 {#v2026-10-12-5}

*12 October 2026*

- **Fixed:** the console made no sound at all, in menus or games, after
  updating to 2026.10.12.3 or 2026.10.12.4. Sound works again.
  ([#351](https://github.com/MMagTech/cabinetos/issues/351))

## 2026.10.12.4 {#v2026-10-12-4}

*12 October 2026*

- **New:** the diagnostic report (Settings, **System**) now includes your
  sound setup, so a "no sound" report can show where it goes wrong.
- Known problem in this version: no sound. Fixed in 2026.10.12.5.

## 2026.10.12.3 {#v2026-10-12-3}

*12 October 2026*

- **New:** the volume keys on a keyboard (or a Steam Deck's volume buttons)
  change the console's volume, in menus and every game. The console starts
  at full volume each time, so it can never be left silent with no way back
  from a controller. ([#351](https://github.com/MMagTech/cabinetos/issues/351))
- **New:** RomM's smart collections show in **Library**, **Collections**.
  Your own collections come first, then each other person's public ones under
  their name; **L2** and **R2** jump between people. Another person's
  Favorites stays private. ([#352](https://github.com/MMagTech/cabinetos/issues/352))
- Known problem in this version: no sound. Fixed in 2026.10.12.5.

## 2026.10.12 {#v2026-10-12}

*12 October 2026*

- **Fixed:** a GameCube save kept only on your RomM server (after a fresh
  install, or on a second console) loaded as an empty memory card. It loads
  now, and saves made from now on reach your server.
  ([#344](https://github.com/MMagTech/cabinetos/issues/344))

## 2026.10.11.4 {#v2026-10-11-4}

*11 October 2026*

- **New:** games on several discs play as one game, with one save that
  carries on from disc to disc. When the game asks for the next disc, open
  the pause menu and choose **Change disc**: it lists the game's discs with
  the one in the drive marked; pick one and choose **Insert**. The game page
  shows which disc you're on. For PS1, Saturn, Dreamcast, Sega CD, 3DO and
  GameCube; put a game's discs in one folder on your RomM server. See
  [Games on several discs](discs.md).
  ([#162](https://github.com/MMagTech/cabinetos/issues/162))

## 2026.10.11.3 {#v2026-10-11-3}

*11 October 2026*

- **Fixed:** in a system's game list, the letter column showed only near the
  top of the list, and some letters were out of order. It now shows wherever
  you are, in your server's order, and holding **L2** or **R2** keeps jumping
  letter by letter.

## 2026.10.10.10 {#v2026-10-10-10}

*11 October 2026*

- **Fixed:** an Xbox controller connected for the first time after a restart
  could count as two players, so every press happened twice (the pause menu
  opened and closed at once). ([#318](https://github.com/MMagTech/cabinetos/issues/318))
- **Fixed:** newer Xbox controllers (Series X|S) would not pair over
  Bluetooth. They pair now.

## 2026.10.10 {#v2026-10-10}

*10 October 2026*

The first release. See its
[release notes](https://github.com/MMagTech/cabinetos/releases/tag/2026.10.10).
