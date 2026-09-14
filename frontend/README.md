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

## What is not here yet

- **Text.** There is no font layer, so captions and titles are placeholder bars
  sized to the space the real type will occupy. Until then nothing in this
  program can be read, only looked at.
- **Cores.** No libretro core has been built for Linux yet. The frame loop,
  the core pacing and the audio governor land with the first one.
- **Images.** Cover art arrives with the RomM client in Phase 4; the cards are
  flat colours for now.

## A warning about motion

Judge it on the reference hardware, never here. The VM has no usable GPU, so
this renders on llvmpipe, and an animation tuned against software rendering is
tuned against the wrong feedback. Build the motion from the numbers in
`docs/PROJECT.md`; look at it on the SER5.
