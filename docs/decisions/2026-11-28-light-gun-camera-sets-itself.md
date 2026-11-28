# A light gun's camera is kept by the console, by what the gun reports

- **Decided:** the console, not Sinden's software, keeps each gun's camera
  exposure (#575 rule 12). It tries eleven exposures from 1.5 to 16 ms and
  keeps the middle of the longest run at which the gun's readings stayed on
  the screen for nine tenths of each look; the lower of two middles. A sweep
  runs from the calibration screen, and in a light-gun game when the gun's
  reading flips to the screen's edge six times in two seconds, or the
  trigger is pulled three times in two seconds with it never on the screen;
  no more than once in 30 seconds. The value is kept per gun (USB id and
  port) and per TV (its EDID's maker, product code and serial), and set
  again whenever the camera is found at another, as after Sinden's software
  starts again. Cameras are touched only while a light-gun game shows its
  border and is being played, or the calibration screen is open. On a TV
  with nothing remembered, a camera starts at 39 (3.9 ms), not Sinden's 78.
- **Date:** 2026-11-28
- **Why:** a fixed exposure failed within one morning on the reference
  console: at 08:35 the gun saw the border from 10 to 160, at 09:50 the room
  brighter it lost it three readings in four at Sinden's 78 and saw it 510
  of 510 at 40; at 11:00, the windows open, both guns saw it from 10 to 50,
  78 lost it about half the readings for one gun and 120 and 160 for both.
  39 is the one rung inside what all four sittings saw (the night's 15 to
  160 included). The gun reports its aim only on a change, so how often it
  speaks says nothing; but Sinden reads exactly 0 or 32767 on an axis when
  the border is not seen, so the latest reading says whether the camera last
  saw it, a still gun included. A gun aimed away reads the same, which is
  why only trouble starts a sweep in a game. Above 16 ms the camera slows
  (52 frames a second at 20 ms). The middle leaves the most room for the
  light to change either way; the lower of two, since a brighter room is
  the change measured. Rejected: Sinden's own exposure setting from the
  launcher (set once per start, and a per-gun, per-TV value would have to
  be handed to it); its automatic exposure (Sinden's file offers it, and a
  camera averaging a bright room dims the border); a slow walk to a
  neighbouring exposure in play (a lost border needs the whole range found
  again, and a walk spends as long at bad exposures).
- **Numbers:** #575, "Sinden light guns: plug in and play gun games
  (waiting on the guns)".
- **Replaces:** none.
