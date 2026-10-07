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
switches a Remote off when a game lets it go. And it keeps the Miis a game sends
to a Remote on the console instead (#275): the Remote's Mii area is answered from
one file per player light, in the folder the console app gives it as its one
argument (`/var/lib/cabinetos/wii-remote-miis`); the Remote is never written
there.

Build (in the builder container): `make`.

The image carries it as `/usr/libexec/cabinetos-wii-bridge`, which is where the
console app looks (frontend/src/wiiremote.cpp). CI builds it beside the frontend
(.github/workflows/build-frontend.yml), ci/stage-image-payload.sh collects it,
and build_files/install-frontend.sh installs it in the frontend layer.
