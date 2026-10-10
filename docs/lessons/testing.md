# Lessons: testing on the A9 and the TV

Read before judging anything, testing on the A9, or driving the TV.

## The thirty-second loop on the TV


**`tools/ui-loop.sh` is the fast loop and I did not find it for most of a
session.** MMagTech, 2026-09-22: *"i need a way to visualize ui additions and
changes or test features or bugs on the a9 without waiting 50 minutes."* That
tool already did it, and an evening of llvmpipe screenshots was spent because
nobody had written it down anywhere a fresh assistant would look.

```
tools/ui-loop.sh                              Home, on the television, picture back
tools/ui-loop.sh --args "--screen accounts"   any screen the frontend can open
tools/ui-loop.sh --game 305 --menu            a game, with the pause menu over it
tools/ui-loop.sh --args "--glow strong" --no-build    tune a number, no rebuild
tools/ui-loop.sh --restore                    put the console back on the image
```

**About 28 seconds**, on the A9's own Radeon, under real gamescope, at
3840x2160. The capture is the frontend's own `SIGUSR1` rather than
`gamescopectl`, which cannot see overlay planes.

**IT BUILDS ON THE CONSOLE NOW, 2026-09-22.** The A9 has 24 cores and 26 GB
against the VM's 5 and 3, and a FULL clean frontend build there takes **6.5
seconds** — less than an incremental one on the VM. The binary never crosses a
machine. `--via-vm` keeps the old route for when somebody is watching
television.

**THE DROP-IN IS TRANSIENT.** It lives in `/run/systemd/system/`, so a
forgotten `--restore` is undone by the next reboot rather than leaving the
reference console on a hand-built binary for ever — which is the trap that was
cleaned up on 2026-09-21.

### WHAT THE LOOP IS FOR, AND WHAT IT IS NOT

**Is it the frontend? Thirty seconds. Is it the operating system? Build an
image.** Cores in `/usr/lib/cabinetos/cores`, systemd units, `tmpfiles.d`, the
session script, base-image changes, first run on a virgin machine and the
installer only exist in an image. Everything in `frontend/src` does not.

A new core is a file copy: the loop launches with
`--core-dir /var/home/cabinet/cores-dev`, so building a `.so` and dropping it
there makes it playable on the television without an image.

### AND THE RULE THAT MAKES THE FAST PATH SAFE

**Do not conclude from something CI could not reproduce.** That is narrower
than "be careful with hand-built binaries", and the difference matters —
MMagTech made the point and the record backs it.

**Seven of the lessons below are about judging in an environment that was not
the real one** — llvmpipe, offscreen, no session, no gamescope — including one
marked *"AND THIS IS THE SECOND TIME"*. **Not one is about a hand-built
binary.** The loop removes that whole class, so using it more is the
correction, not a new risk.

The one case that did burn this project — an unshippable PlayStation 2 core
sitting in a home directory, which a session concluded meant PS2 was solved —
happened on the A9, with a GPU, and ran perfectly. What was wrong was that its
source repository does not exist, so **CI could never build it.** A frontend
compiled from `frontend/src` is the same source CI compiles; that is the line.

**And nothing in this loop can reach an end user.** Checked 2026-09-22: no
workflow, no script in `ci/` and nothing in `build_files/` references either
machine, `scp`, `rsync` or `ssh`. The image is built by GitHub's runners from
the repository alone.

## Judging what you see

- **Judge nothing visual on the VM.** Software rendering on llvmpipe. And it is
  not only motion: a television's overscan eats more vertical room than a
  framebuffer capture shows. **Vertical fit cannot be judged here either.**

- **Read the pixels before believing the picture.** PPSSPP's first capture was
  not blank — it was a plausible, nearly-black rendering with faintly legible
  text. The maximum pixel in the whole 1920x1080 frame was RGB **(4,4,4)**.

- **Every screen photographs itself, headless.** `SDL_VIDEODRIVER=offscreen`
  needs no compositor, no session and no controller:
  ```
  SDL_VIDEODRIVER=offscreen ./build/cabinetos-frontend --romm 192.168.1.10:6005 \
    --screen library --screenshot /tmp/x.bmp --render-size 1920x1080 --frames 60
  ```
  `--screen` opens by walking the route a person walks, so a capture cannot show
  a state the product cannot reach. `--storage`, `--download` and `--unkeep` do
  the same for the things with no picture.

- **`--frames N` only ends the run when there is a `--screenshot` to take.**
  Without one the loop never exits and the command sits there at full CPU.

- **`--launch-after` IS WALL-CLOCK SECONDS AND THE UI LOOP IS NOT PACED**, so
  on the A9's Radeon a run of 1600 frames goes by in under a second and the
  launch never fires at all — no error, no `[launch]` line, just a screenshot
  of Home. It worked on the VM the whole time because llvmpipe is slow enough
  to take longer than a second. **Use `--launch-after 0`.** Twenty minutes went
  on this, chasing a difference between two binaries that did not exist.

- **The same capture command is worth running ON THE A9**, with
  `SDL_VIDEODRIVER=offscreen` and `--render-size 3840x2160`. It uses the real
  Radeon, needs no compositor, and does not disturb the running session — so a
  4K frame off the reference GPU costs one command and no downtime.

- **`--storage-root <path>` puts a whole console somewhere else**, which is how
  the two-disk and the out-of-space tests were run without disturbing the real
  tree.

- **`$CABINETOS_ROMM` works everywhere `--romm` does**, which is often shorter.

- **To watch a real game, launch it**: `--launch <romId> --launch-after 1
  --frames N`. A PSP game needs about 2500 drawn frames to reach its attract
  demo on this VM; Dreamcast about 1400. Add `--overlay-exit` to make it quit
  back to Home by itself, which is the only way to exercise the unload path
  without a controller.

- **Stop the session before building on the VM.** The frontend runs at 300% CPU
  under llvmpipe and it is four cores. Or skip it and use the offscreen driver.

- **AN OFFSCREEN CAPTURE DOES NOT PROVE A WINDOW EVER GETS A FRAME.**
  `Renderer::beginFrame` binds an offscreen SCENE target so panels can blur what
  is behind them, and **`presentScene()` is what puts it on the real
  framebuffer**. Miss that call and the loop runs perfectly at sixty frames a
  second presenting nothing — while every `--render-size` capture comes out
  correct, because `saveFrame` reads the offscreen target directly. The
  television showed white, gamescope's own screenshot came back entirely black,
  the process sat at 5% of a core, and nothing logged an error. **Anything that
  draws a screen must call `presentScene()`, and anything drawn after it lands
  on top of the scene rather than inside it.**

- **AN OFFSCREEN CAPTURE DOES NOT PROVE THE SESSION WORKS, AND THIS IS THE
  SECOND TIME.** The existing note about `presentScene()` is the same lesson
  one layer down. **Anything touching the display path has to be run under
  gamescope before it is believed** — put `--launch <id> --launch-after 0` in
  the session drop-in, which is how both of these were finally caught.

- **A SETUP SCREEN'S LOOP MUST BE PACED, AND `--frames` DEPENDS ON IT.** A page
  of static text left unpaced runs at thousands of frames a second on the A9,
  and four hundred frames went by before the server had answered — so the
  capture of the pairing screen came out with no code on it. **The same trap
  `--launch-after` fell into, one screen along.**

## The A9 and the TV

- **DO NOT DIAGNOSE HARDWARE FROM A PHOTOGRAPH.** Two theories were built and
  discarded here — one from a filename nobody checked, one from a digit
  misread off a picture of a rotated monitor. Get a shell and read `dmesg`.

- **`journalctl -u cabinetos-session` shows almost nothing.** The script's own
  output carries the syslog identifier, so the unit filter returns only
  systemd's start/stop lines. Use **`journalctl -t cabinetos-session`**. And
  the clock jumps when NTP syncs after an install, so `--since` lies on the
  first boot; use `-n`.

- **gamescope does not pick the display's mode on its own.** With no
  `--output-width`/`--output-height` at all it still chose 1920x1080 on a
  3840x2160 panel. If you want native, read the connector and pass it.

- **A 4K panel is the default assumption now, not a possibility.** PROJECT.md
  always said most sets are 4K; the first one plugged in was.


**SSH TO THE A9 IS PORT 2222 SINCE THE FILE ACCESS IMAGE, and sudo's
password is in `/var/lib/cabinetos-files/password`**, not `cabinet`. Every
`ssh ... cabinet@192.168.1.212` below needs `-p 2222`.

**The A9 can be pressed from the Mac, so MMagTech can watch while the
assistant drives.** `cabinet` can write `/dev/uinput` and the image ships
`ydotool`. Start the daemon once per boot:
`nohup ydotoold --socket-path=/tmp/ydotool.sock --socket-own=1000:1000 &`,
then `YDOTOOL_SOCKET=/tmp/ydotool.sock ydotool key 108:1 108:0` (Down; Up
103, Left 105, Right 106, Return 28, Escape 1, Backspace 14) or
`ydotool type '192.168.1.10:6005'`. The frontend takes a keyboard everywhere.

- **The frontend's SIGUSR1 capture only works in the main loop.** First run
  and the startup screen have loops of their own: use `gamescopectl
  screenshot` with `XDG_RUNTIME_DIR=/run/user/1000` and
  `GAMESCOPE_WAYLAND_DISPLAY` set to the NEWEST `gamescope-N` there; a
  session restart leaves the old socket behind and gamescopectl fails on it.

- **After five idle minutes the first press only wakes the screen,** and
  every later press lands one step off. That opened the Wi-Fi panel instead
  of Sign out once. Capture before any press that destroys something.

- **A restart in place keeps the loop's `--args`**, so a test deploy with
  `--screen settings` comes back on Settings, not Home.

- **`gamescopectl` IS NOT ALWAYS ON `gamescope-1`.** The socket number is
  whichever the current instance took, and it changes when the session
  restarts. List `$XDG_RUNTIME_DIR` and use the one whose mtime matches the
  running gamescope; stale sockets from earlier instances sit there looking
  identical.

- **`Environment=` IN A SYSTEMD DROP-IN SPLITS ON WHITESPACE.** An unquoted
  `CABINETOS_APP=/path --core-dir x` sets the path and silently discards every
  argument, and the console comes up looking correct on the default core
  directory. Quote the whole value.

- **`sudo -S ... | tail -0` HIDES A FAILED SUDO.** Three restarts in a row did
  nothing and the journal kept showing the old process, because the output
  that would have said so was thrown away. Print the exit status.

- **A BINARY THAT IS RUNNING CANNOT BE OVERWRITTEN** — `cp` fails with "Text
  file busy" and the restart brings back the OLD build, which reads exactly
  like the fix not working. Stop the session, copy, start.

- **NEVER COPY A NEW `.so` OVER ONE A RUNNING PROCESS HAS LOADED.** The
  process has the file mapped, and writing into it kills the process with
  SIGBUS (2026-10-04: the console died in `UpdateTargetSpeed` inside
  `cabinetos-ps2.so`, the session restarted the game by itself, and the
  scripted presses that followed landed on the wrong screens). Put the
  console on Home first (`tools/ui-loop.sh --no-build`): PS2 and the cores
  are loaded only when a game starts. `coredumpctl list` shows it.

- **gamescope crashes on its way out when the app is stopped**
  (`wl_display_destroy`, SIGSEGV or SIGABRT). It is every session stop, not
  the thing being tested; look at what stopped the app.

- **frames.py CANNOT SEE A REPEATED GAME FRAME.** The console presents at
  every refresh whether the game made a new picture or not, so gamescope's
  frame times read a clean 16.67 ms over a game that repeats and skips
  frames every second. Read the console's own `[pace]` line (#221).

- **`--env` REACHES THE FRONTEND, NOT THE SESSION SCRIPT.** It goes on the
  frontend's command line, so `CABINETOS_OUTPUT` given that way changes
  nothing and the TV stays at 4K. `tools/ui-loop.sh --session-env` puts it
  on the session. The LG's 2560x1440 mode is 59.95 Hz, the one non-60.000
  rate this TV has for testing.

## Logs


`journalctl -u cabinetos-session` shows ONLY systemd's own start/stop lines and
none of the frontend's output, which reads exactly like a console that does not
log. It does. The frontend's lines are in the journal without that unit
attached, so ask for the journal itself and grep it:

```
sudo journalctl --since "-40 min" -o cat | grep -aE "\[launch\]|\[core\]|\[frontend\]"
```

That one command is the difference between diagnosing a launch failure in a
minute and guessing at it for twenty.

## Testing emulators headless

- **`--core-option k=v` only reaches the `--core <so> --rom <file>` path.**
  A `--launch <id>` run ignores it and uses `catalog::optionOverrides` only.

- **`--core-options-detail` sees only options declared at load.** FCEUmm
  and FBNeo declare theirs with a game loaded and show "0 options"; read a
  real launch's `[options] N declared, M asked` instead.

- **`--state-test` cannot see a Vulkan picture.** It hashes a GL frame, so
  on ParaLLEl-RDP it reports "picture STATIC" and then PASS; that PASS
  proves nothing about the picture.

- **Headless games sometimes open their own pause menu** and, once, chose
  Exit to Home after 6 s (Dreamcast). A headless run that goes silent or
  leaves is the rig until the TV says otherwise (Virtual Boy did this and
  was fine on the TV).

- **`~/fb/sweep.sh` launches the smallest game of every system** from
  `~/fb/sweep.txt` (`~/fb/sweep-all.txt` is the full list) with dummy audio
  and writes `~/fb/sweep/summary.txt`. About 25 minutes for 30 systems. **It
  hammers the GPU: never run it while MMagTech is testing on the TV.** It
  made Mario Kart look "really bad" once.

- **A frame capture of a game has a pause menu over it** when the headless
  window loses focus. Read the picture around it.

- **A headless `--launch` on the A9 is a real play as far as the drive is
  concerned.** It touches the game's entry, so the game jumps to the front
  of the drive's last-used order: the offline Recent shelf (#88) and the
  order the cache is cleared in. 2026-10-05: eight test launches reordered
  the console's offline Home and the dates had to be put back by hand with
  `touch -h -d`. Note an entry's time (`stat -c %Y`) before a launch test and
  restore it after, or test with the game that is already first. Kill with
  SIGKILL before two minutes and no play session is recorded.

- **A headless run on the A9 reads the real controllers.** Pads and Wii
  Remotes go to every frontend on the machine, not only the one on the TV.
  2026-10-05: while MMagTech played at the TV, a headless test copy took his
  presses, opened its power menu and started Wild West Guns offscreen (it
  restored a save folder; nothing was lost). **Never run a headless frontend
  while he is at the TV**; use `tools/ui-loop.sh` and let him watch instead.

- **Offline is tested with a wrong address, never by stopping RomM.**
  `CABINETOS_ROMM=192.168.1.10:6099` (headless, or `tools/ui-loop.sh --env`)
  is offline from boot. For the server COMING BACK, point the console at a
  relay on the A9 itself, `CABINETOS_ROMM=127.0.0.1:16005` with
  `tools/offline-relay.py` copied to the A9 (it forwards to RomM): relay off is no server, relay
  on is the server back. Covers are filed by address, so link
  `covers/<that address>` to `covers/192.168.1.10_6005` for the test and
  remove the link after.

- **Reading a game with `GET /api/roms/<id>` makes RomM create that person's
  empty `rom_user` row**, which moves its `updated_at`. Harmless, but do not
  read it as something the console sent.

### TWO GAMES AT DIFFERENT SOUND RATES, WITHOUT A RESTART — learned 2026-09-27

The audio stream was opened once, at the first game's sample rate, and kept
for every later game until the app restarted. It hid for eight days because
**every loop deploy, image update and reboot restarts the app**, so most
tests played one game in a fresh app, and a fresh app is always right. It
showed as DoDonPachi (47997 Hz) after Mortal Kombat II (32040 Hz): no sound
while playing, then the backlog playing on over Home after quitting, which
MMagTech read as "another version is running". **Any per-game setting that is
opened once and reused needs a test of two different games in one run.**

## Habits

- **Measure rather than reason, where you can.** Two minutes of measurement has
  beaten a plausible argument every time it has been tried here.

- **Run the control.** `--core-options-off` and `cores/backend-diff.sh` exist
  for it, and the control has now been more informative than the result three
  times — most recently the bare-`fedora:44` library sweep.

- **`/tmp` on the VM is a small tmpfs.** Copying 273 MB of cores into it fails
  with "Disk quota exceeded" halfway. Use `/var/mnt/games/` for anything large.

- **`pgrep -f "some string"` matches your own command line**, and so does
  `pkill -f`. **Match on something the checker cannot contain** — `pgrep -x`, a
  pid file, or the exit status of the thing you started. `pgrep -x` also
  refuses names over 15 characters, so `cabinetos-frontend` needs
  `ps -eo args | grep "[c]abinetos-frontend"`.

- **Two podman containers with `:Z` over overlapping paths will break each
  other.** `:Z` relabels the whole mounted tree for one container's SELinux
  category. Cost one PPSSPP build. **Do not start a second container over a
  parent of a running one.**

- **A loop deploy restarts gamescope, and every `gamescopectl` setting goes
  with it.** 2026-10-07: `composite_force` was on for a Remote Play stream;
  `tools/ui-loop.sh` restarted the session, the setting was gone, and the
  phone's stream went black while the phone stayed "connected". Anything set
  by hand on the running gamescope is gone after a deploy; set it again, or
  make the thing that needs it set it.

- **A black stream with sound: capture gamescope before blaming the
  stream.** 2026-10-08, a game in Steam: a `gamescopectl screenshot` showed
  the game drawn normally while the phone was black, which put the fault in
  Sunshine's capture, not the game or the TV; Sunshine's own line said which
  ("Mapped 'HDMI-A-1' to kmsgrab monitor index 1": it numbers planes, and 1 is
  the overlay). A guess carried from the previous day (Steam's gamescope never
  composed) was half wrong: Steam's menus streamed without it. And ask what
  the TV shows too: one black TV that evening was a second, separate fault.

- **Sunshine picks its plane only at certain moments, so time those against
  gamescope, not just the end state.** #304, 2026-10-09: back from Steam the
  stream went black although the console composed every frame. Sunshine had
  picked 0.35 s after the new gamescope came up, 0.7 s before it composed,
  while Home was still on two planes. The proof took a 5 ms plane sampler
  (libdrm through python ctypes, as `cabinet`, no root) run across a session
  restart: it shows which planes carry a picture from gamescope's first
  frame. Line Sunshine's "Reinitializing capture" and "Mapped ... index N"
  up with the session's "is up" and the moment composing starts.

- **Read a person's one-word answer against the question, not the hope.**
  "nope" to "is the seam still there?" was read as "gone", a fix was
  declared, and a comment saying so went on #286 before "no the issue is
  still there" corrected it. When a yes/no question can be read both ways,
  ask it as "yes or no: is X still there?", and verify on the machine before
  writing it down.

- **"The colours look washed out" on a stream: read the stream's colour, not
  the program's version.** #304, 2026-10-09: a freshly built Sunshine was
  blamed. Sunshine logs the colour of every stream ("Color coding: SDR
  (Rec. 601)" or "HDR (Rec. 2020 + SMPTE 2084 PQ)"), and the TV's HDR state
  is the connector's HDR_OUTPUT_METADATA (eotf 2 is HDR10). Steam runs the TV
  in HDR; composed, the copied picture is HDR, so an SDR stream (Moonlight's
  HDR setting off) looks washed out, and with it on the stream matches the
  TV. The richer-looking picture was the wrong one. Ask which way the phone
  was set before changing anything, and when two pictures are compared,
  have the person compare each against the TV, not against each other.
