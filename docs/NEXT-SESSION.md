# Picking this up

Written at the end of each session for whoever starts the next one, which is
usually a fresh assistant with no memory of what just happened.

**`docs/PROJECT.md` is the specification and it is authoritative.** This file is
only the short version: what state things are in, what to do next, and the
handful of things that will waste a day if nobody says them out loud.

Rewrite it at the end of a session. It is meant to be current, not a log.

---

Read `docs/PROJECT.md` first — all of it. It is the specification and it is
current. Start with "Where the project is", which tells you what runs, what does
not, and what has never been tested.

Then read `frontend/README.md` for the build and run loop. **Nothing builds on
this Mac** — the frontend is built in a container on the test VM and run there.

## Where things stand

The frontend is real and running on the VM: C++20, SDL3, one EGL/GLES 3 context,
no toolkit. It has the design system's focus treatment and motion, text with a
CJK fallback, cover art with an async budgeted cache, frosted glass, an
on-screen keyboard, and a libretro core host. **Dr. Mario runs on it, with
save states.**

The biggest question in the project is answered: **save states are portable
between Cabinet and CabinetOS**, proved by building Gambatte for Linux at the
commit Cabinet's Mac build is pinned to and cross-loading states both ways with
a control run.

**Two cores now build in CI** — `gambatte` and `genesis_plus_gx` — on a GitHub
runner from a bare checkout, in about a minute each. The workflow asserts the
finished `.so` reports its pinned revision as its own version string, not merely
that the checkout was at it (`tools/core-info.c`). Adding a core is a `case` arm
in `cores/build-core.sh` plus a name in the matrix in
`.github/workflows/build-core.yml`.

**There is read-only access to the live RomM server**, obtained through the same
device-approval flow Phase 4 has to implement. Token at
`~/.config/cabinetos/romm-token.json`, 0600, on the Mac — **not** on the VM and
not in the repo. Six read scopes, no writes, no expiry. The VM should get its
own token rather than a copy, so they revoke independently.

## Pick up with one of these

They are independent. Do not try to do them all.

**1. Measure the Sega CD save-state question.** `genesis_plus_gx` is built with
`HAVE_CDROM=0` to match Cabinet, and **that is a decision made from reading, not
a measurement**. The library has 22 Sega CD titles, so it is now testable:
`tools/state-probe.c` on the VM, a state written by each build, cross-loaded. If
the flag turns out not to touch the state format, say so in PROJECT.md and stop
carrying the caveat.

**2. The backend-sensitive cores.** pcsx_rearmed, melonDS, Flycast, picodrive,
where the Linux default turns on a recompiler Cabinet's build has off. **That is
where the remaining parity risk lives.** The lever for each is in PROJECT.md's
table under open question 13; the pin and the build arguments are in the
manifest.

**3. Phase 4, the RomM client.** The keyboard exists, the server is reachable,
and the auth flow has now been walked end to end by hand — so the shape is
known. Copy Cabinet's two-screen flow rather than inventing one: address, then a
QR code to approve. Read `RommApp/RommApp/Auth/RommClient.swift` in the Cabinet
checkout before writing anything. **Note the HTTP requirement** recorded in
Phase 4: accept a bare host, probe the scheme, never refuse plain HTTP. **And
the platform-identity rule**, also in Phase 4 — key by `id`, not by `slug`.

**4. More screens.** Home's hero and shelves, the library grid, game detail,
settings. The design system has exact numbers for all of them and the components
exist.

## Two small things left open

- **A white line was reported under the keyboard's title and could not be
  reproduced.** The captured framebuffer has no bright horizontal run anywhere
  in that band — the strongest edge is the field's own top boundary, which can
  only darken. Likely a VNC scaling artefact. **Ask which line before chasing
  it**; do not go hunting on the strength of this note alone.
- **The keyboard wraps horizontally but not vertically.** That was a deliberate
  split — five rows is short enough to cross directly, and wrapping up from the
  space bar would skip the letters. Worth re-judging with a controller in hand
  rather than from a screenshot.

## Things that will bite you if nobody says them

- **Judge nothing visual on the VM.** It renders in software on llvmpipe. Motion,
  the letterbox glow and the safe area are all recorded as needing the SER5 on a
  real television. An animation tuned in the VM is tuned against the wrong
  feedback, and the game runs in slow motion there by design.
- **Read `build_args` from the manifest before building any core.** Do not infer
  an empty `MAKEARGS` from a core's absence from PROJECT.md's recompiler table.
  Three divergences are known and only one class is recompilers:
  `genesis_plus_gx` needs `HAVE_CDROM=0` and `vecx` needs `HAS_GPU=0`, both
  because the `unix` branch asks `uname` what machine it is on and changes the
  build.
- **Configuration is keyed by PLATFORM, not by core** — Genesis Plus GX serves
  four platforms with different option tables and pad types. And the converse:
  **two platforms can share a name and a slug**. "Arcade" is two platforms in
  RomM, FBNeo and MAME 2003-Plus, deliberately. Key by `id`.
- **Glass does not nest.** One glass layer per modal; everything on it is an
  ordinary surface.
- **Anything a person must read is Callout (31pt) or above.** Caption is for
  glancing at.
- **Design rules are not universal — check which surface they apply to.** "No
  wrapping at the edges" was right for a shelf and wrong for a keyboard, and it
  was written down as a blanket rule before anyone noticed. Consult what
  PlayStation, Xbox and Steam actually do before inventing an interaction.
- **`dlerror()` clears itself when read.** Read it once.
- **A test must first prove the thing it measures actually varies.** The save
  state test passed while proving nothing, because it compared video on a static
  title screen. It now asserts the picture is moving and reports INCONCLUSIVE
  rather than a difference when there is no audio.
- **Look on disk before concluding a file does not exist.** `core-manifest.json`
  is at `~/Downloads/core-manifest.json` and is not on GitHub. A whole round trip
  was wasted searching `raw.githubusercontent` and the GitHub code index and
  reporting it unavailable, while it sat in Downloads.

## Two Cabinet-side debts, not CabinetOS's to fix but its problem

1. **Flycast carries unscripted edits in its working tree**, so its pinned commit
   does not reproduce what ships — for Dreamcast and Naomi, and for one of only
   two cores that can answer the parity question cleanly. Capture that diff
   before anything touches the tree.
2. **`core-manifest.json` is still not pushed to GitHub.** It is now load-bearing
   for every core after the second, and it is a single unbacked file on one Mac
   holding revisions that exist nowhere else — the same single-machine failure
   the recovery exercise was run to fix.

## How the user wants this done

Plain answers. Lead with the decision, keep the reasoning in `docs/PROJECT.md`.
Check the running machine before theorising — most wrong turns come from
reasoning off an error message instead of looking. Say plainly what is verified
and what is assumed; they notice and ask. And they push back usefully: "are we
sure we can't do X?" has repeatedly produced a better answer than the first one.

**Write commit messages and PR bodies so someone can tell what the change does
and why it exists without already knowing this document.** A PR from this
session was reviewed and came back as "completely foreign to me what it did and
what it exists for" — the work was right, the writing assumed too much. Open
with the problem in product terms, then what was added, then any judgement call
worth checking. Matching PROJECT.md's dense register is not the same as being
understood.
