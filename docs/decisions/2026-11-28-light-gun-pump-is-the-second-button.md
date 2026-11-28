# A light gun's pump is its second button, never a shot

- **Decided:** in a light-gun game the pump, and the front left button with
  it, reaches every emulator as the gun's second button (libretro's `AUX_A`)
  and as nothing else. The console never answers libretro's `RELOAD`, a shot
  off the screen. A game reloads as on the cabinet: the trigger, aimed off
  the picture.
- **Date:** 2026-11-28
- **Why:** #575 rule 10 makes the pump the game's second button, for a
  reload or a grenade where the game has one. Both MAMEs make `RELOAD` a
  trigger pull: MAME 2003-Plus off the screen, MAME 0.289 where the gun
  aims. Answered too, the pump would shoot as well as press button 2, so in
  Beast Busters a grenade would come with a wasted shot, and in MAME 0.289
  the pump would fire. FinalBurn Neo never reads `RELOAD`, so it loses
  nothing. Aimed off the picture, the gun already says it is off the screen,
  which is what every emulator reloads on.
- **Numbers:** #575, "Sinden light guns: plug in and play gun games (waiting
  on the guns)".
- **Replaces:** none.
