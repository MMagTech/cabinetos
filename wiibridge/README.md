# cabinetos-wii-bridge

A stand-in for each real Wii Remote, so Dolphin and Cemu get a Remote that works
(issue #200). Its own program, and **GPL-2.0-or-later, not MIT**, because it
carries Dolphin's extension encryption (`encryption.cpp`/`.h`, from
libretro/dolphin 1a0f97270b, credited in each file). The console app (MIT) only
starts it.

What it does and why is at the top of `bridge.cpp`. In short: it keeps a copy's
Nunchuk unencrypted (copies drop it when a game writes the encryption key) and
encrypts for the game itself; gives every Remote Nintendo's id (Cemu looks only
for that); keeps the stand-in when a Remote goes off (libretro's Dolphin crashes
when a held Remote vanishes) and replays the game's setup when it comes back; and
switches a Remote off when a game lets it go.

Build (in the builder container):

    g++ -std=c++20 -O2 -Wall -Wextra -o cabinetos-wii-bridge bridge.cpp encryption.cpp

Not yet built by the image; see docs/NEXT-SESSION.md on branch wii-remotes.
