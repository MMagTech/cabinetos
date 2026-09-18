# The CabinetOS frontend

The emulator host with the UI on top. See `docs/PROJECT.md` — *The design
system*, *How Cabinet hosts cores* and *The frontend toolkit* — for what this is
and why it is built this way.

C++20, SDL3 for platform, input and audio, one EGL / OpenGL ES 3 context, and a
UI layer of our own. No toolkit between us and the frame, because the frame is
the part that must not go wrong.

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

An entry is a FILE when the game is one file and a DIRECTORY when its archive
unpacked into several. See `src/storage.h` and `docs/PROJECT.md`, open
question 18.

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
- **The rest of the launch screen**: a different save state, a different core,
  an export.
- **The Storage screen.** Its data exists; `--storage` prints it.

## A warning about motion

Judge it on the reference hardware, never here. The VM has no usable GPU, so
this renders on llvmpipe, and an animation tuned against software rendering is
tuned against the wrong feedback. Build the motion from the numbers in
`docs/PROJECT.md`; look at it on the SER5.
