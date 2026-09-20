# The CabinetOS frontend

The emulator host with the UI on top. See `docs/PROJECT.md` — *The design
system*, *How Cabinet hosts cores* and *The frontend toolkit* — for what this is
and why it is built this way.

C++20, SDL3 for platform, input and audio, one EGL / OpenGL ES 3 context, and a
UI layer of our own. No toolkit between us and the frame, because the frame is
the part that must not go wrong.

## Where the binary ends up

**In the image, as of 2026-09-19.** `/usr/bin/cabinetos-frontend`, with the
twenty-one cores at `/usr/lib/cabinetos/cores/`, put there by
`build_files/install-frontend.sh` from what CI built. Before that the image
contained none of it and an installed machine booted to a black screen, so
this binary existed only on the test VM.

That means **a change here now rebuilds the image**, and a machine running
CabinetOS picks it up with `bootc`. It also means the loop below is still the
loop: eleven seconds against thirteen minutes.

## Building

**Not on the Mac, and not on the console.** The development machine cannot build
Linux binaries, and the OS image carries no compiler by design — it is a
console. So the build happens in a container, on the test machine or in CI:

```bash
podman build -t cabinetos-builder -f Containerfile .
podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make
```

The result is `build/cabinetos-frontend`. `Containerfile` pins Fedora 44, which
is what Bazzite 44 is built from, so glibc and every runtime library match the
image the binary runs on. **If the base image's Fedora release moves, move that
with it.**

## Where it puts things

Everything the console holds lives under one root — `/var/lib/cabinetos` when
that directory can be created and written, and the directory the binary was
started in otherwise, which on the test VM means `~/frontend`. `--storage-root
<path>` overrides both, and whichever wins is printed at startup:

```
<root>/
├── roms/<platform>/<romId> - <name>     kept games
├── cache/<platform>/<romId> - <name>    pulled games, the only thing eviction touches
├── bios/                                firmware from RomM, and the core system directory
├── users/<id> - <name>/
│   ├── saves/<platform>/<romId>/<core>/
│   ├── states/<platform>/<romId>/<core>/
│   ├── keeps/  pending/  screenshots/  config/
├── config/
└── logs/
```

`<platform>` is RomM's own `fs_slug` — `Sony Playstation`, `Game Boy`, `FBNEO` —
the same spelling in all four places, and the same one the server uses for its
roms and its assets. An entry is a FILE when the game is one file and a
DIRECTORY when its archive unpacked into several:

```
cache/Sony Playstation/323 - Crash Bandicoot.chd
cache/Game Boy/39 - Tetris/            the zip RomM sent, and the .gb from it
```

See `src/storage.h` and `docs/PROJECT.md`, open question 18.

## Where it looks for the cores, and for a server

Two things the program cannot be told on the command line when it is a console,
because a console has no command line. Both are **tried, and the answer is
printed**, for the same reason the storage root is:

```
[cores] /usr/lib/cabinetos/cores
```

- **The cores.** `cores/build` if that directory exists beside the working
  directory — the development case — and `/usr/lib/cabinetos/cores` otherwise,
  which is where the image puts them. `--core-dir` overrides both. Getting this
  wrong is not loud on its own: the console simply reports every platform as
  *"the core for this system is not built on this console yet"*, which reads as
  twenty-one broken emulators rather than one wrong path. Hence the line above.
- **The server.** Four places, in this order, and the first one that answers
  wins:

  ```
  --romm  →  $CABINETOS_ROMM  →  /etc/cabinetos/session.env  →  config/server.json
  ```

  `session.env` is machine-local and not in the image — this repository is
  public and somebody's LAN address does not belong in it. `config/server.json`
  is what first run writes, because the session user cannot write `/etc`, and it
  loses to the other three deliberately: root's answer must not be silently
  overridden by a file the session wrote. With none of them, the frontend comes
  up on the stand-in library.

  **`session.env` is read directly as well as through the environment**, so a
  binary started over SSH resolves the same address the session does. Without
  that, every probe run over SSH reports a configured console as unconfigured.

## First run, and the network

**Built, start to finish.** Five screens — network, Wi-Fi, the RomM server,
pairing by QR, a Bluetooth controller — plus everything behind them. `--setup`
forces the flow on a machine that is already configured and never writes
anything, which is the only way to look at it here: both machines are set up.

```
--setup                     run it even on a configured machine; writes nothing
--setup-step <name>         open at one step: network wifi server pair controller done
--no-setup                  skip it entirely on a machine that cannot finish it
```

Each mechanism can also be run on its own from a shell. **None of these
disturbs a session already on the television**, and all of them run before SDL,
so they need no window, no GL and no controller.

```
--first-run                 where setup is, and where it would stop
--first-run-check-server    the same, and ask whether the server answers
--first-run-step <name>     open the chain at one step: network wifi server pair controller
--first-run-rules           96 fact combinations, asserting the refusals
--network                   link, radio, and whether a network can still be saved
--network-scan              the same, and what is on the air
--qr "<text>"               a QR code on a terminal, scannable straight off the screen
--qr-out <path.pbm>         and write it as an image
```

**`--first-run-rules` is the test, not a report.** It walks every combination of
facts the state machine can be handed and asserts what must be REFUSED — that
setup cannot finish while offline, with no server answering or with no token;
that Wi-Fi cannot be passed over while offline; that every blocked step explains
itself. It needs no network and no server, so it runs anywhere. It has already
found one real deadlock.

**`--network` reports who is asking, and you should read that line.** The polkit
grant for saving a Wi-Fi network depends on the session the caller is in, so
over SSH it says `auth_admin_keep` and at the console it says `yes` — and both
are correct.

## Running

The OS session already runs `cage`, so the frontend attaches to it as an
ordinary Wayland client. Nothing needs stopping first:

```bash
XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
  SDL_VIDEODRIVER=wayland ./build/cabinetos-frontend
```

Left and right move focus, on a controller's d-pad or the arrow keys. A
controller is required by the product and a keyboard is merely supported, so
both work and neither is needed.

## Seeing what it drew

The test machine has no way to show anyone a screen, and "it looked right here"
is not something this project can say. So the frontend photographs itself:

```bash
./build/cabinetos-frontend --screenshot /tmp/frame.bmp --frames 30 --focus 1
```

`--focus N` starts with card N already focused and settled, so a capture shows
the resting focus state rather than a frame part-way into the transition.

## Checking a television nobody here owns

Most sets are 4K. Plenty are not. Neither may be assumed, and the machine
running the test is neither. `--render-size` renders the whole frame offscreen
at any size and reads it back, so a 1280x800 development VM can prove its layout
at 3840x2160:

```bash
for size in 3840x2160 1920x1080 1280x720; do
  ./build/cabinetos-frontend --screenshot /tmp/shot-$size.bmp \
    --render-size $size --frames 5 --focus 1
done
```

Verified 2026-09-13, all three: identical layout in design points, and the 4K
frame is rendered natively rather than upscaled — the shapes are signed-distance
fields, so an edge is exact at any resolution.

## The development loop

```bash
rsync -az -e "ssh -i ~/.ssh/cabinetos" frontend/src/ cabinet@192.168.1.250:~/frontend/src/
ssh -i ~/.ssh/cabinetos cabinet@192.168.1.250 \
  'cd ~/frontend && podman run --rm -v "$PWD":/src:Z -w /src cabinetos-builder make'
```

Seconds, not minutes. This is what developer mode's SFTP exists for (Phase 6);
until then it is plain SSH, which Phase 1 deliberately left on.

## Seeing a screen with nothing running

The captures above need a compositor. This one does not — no cage, no session,
no controller, no window:

```bash
SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --romm 192.168.1.10:6005 \
  --screen library --screenshot /tmp/x.bmp --render-size 1920x1080 --frames 60
```

`--screen` opens a screen by walking the route a person would walk — the Library
is entered, a tile is opened, the launch screen is opened from a card — so a
capture cannot photograph a state the product is unable to reach.

```
--screen library [--tab 1] [--tile N]
--screen grid --tile N
--screen detail --game <romId>
```

Three things have no picture, and they get the same treatment for the same
reason — this machine has no controller, so the only way to exercise what a
person would press is to press it from here:

```bash
./build/cabinetos-frontend --storage              # free space, floors, who kept what, evictable
./build/cabinetos-frontend --core-options         # every option every core declares
./build/cabinetos-frontend --launch ID --core-options-off   # the control: answer none
./build/cabinetos-frontend --romm HOST --download <romId>
./build/cabinetos-frontend --romm HOST --unkeep <romId>
```

`--download` calls exactly what the launch screen's row calls, floors and all.

## What is not here yet

- **The navigation bar.** The Library is reached with a temporary **L** key.
  It is not built because it costs almost exactly the vertical slack Home has
  left, and that is a measurement only a real television can settle — see
  `docs/PROJECT.md`.
- **Joining a Wi-Fi network has not been done from the console.** `net::join`
  is written and the scan, the status, the polkit verdict and every screen are
  measured on the A9, but actually joining one is not: the reference machine is
  on a cable, and taking it off is how you lose the machine you are measuring.
  Do it with a keyboard at the console.
- **Nobody has walked first run with their hands.** Every screen is captured,
  but the whole of it start to finish on a television has not been done.
- **The rest of the launch screen**: a different save state, a different core,
  an export.
- **The Storage screen.** Its data exists; `--storage` prints it.

## A warning about motion

Judge it on the reference hardware, never here. The VM has no usable GPU, so
this renders on llvmpipe, and an animation tuned against software rendering is
tuned against the wrong feedback. Build the motion from the numbers in
`docs/PROJECT.md`; look at it on the A9 Pro.
