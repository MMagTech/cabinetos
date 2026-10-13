# What's new

Every update that reached consoles, newest first. To update: Settings,
**System**, **System update**. An update you skipped is included in the next
one you install, so you only ever install the newest.

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
