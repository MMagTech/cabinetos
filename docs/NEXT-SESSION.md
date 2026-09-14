# Picking this up

Written at the end of each session for whoever starts the next one, which is
usually a fresh assistant with no memory of what just happened.

**`docs/PROJECT.md` is the specification and it is authoritative.** This file is
only the short version: what state things are in, what to do next, and the
handful of things that will waste a day if nobody says them out loud.

Rewrite it at the end of a session. It is meant to be current, not a log.

---

Read `docs/PROJECT.md` first — all of it. It is the specification and it is
current. Start with "Where the project is", which was rewritten at the end of
the last session and tells you what runs, what does not, and what has never been
tested.

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

## Pick up with one of these

They are independent. Do not try to do them all.

**1. The same core build in CI.** Everything works on one machine, which is
precisely the failure open question 13 exists to prevent. `cores/build-core.sh`
is reproducible and was proved so from a clean checkout; it needs a GitHub
Actions job around it. Small, and it closes a stated gap.

**2. The second core, then the backend-sensitive ones.** Genesis Plus GX next,
because one build covers four platforms — but confirm each of the four
separately, since Sega CD saves by a different mechanism than the other three.
Then pcsx_rearmed, melonDS or Flycast, where the Linux default turns on a
recompiler Cabinet's build has off. **That is where the remaining parity risk
lives**, and `tools/state-probe.c` is the instrument for it.

**3. Phase 4, the RomM client.** The keyboard exists now, so first-run setup is
reachable. Copy Cabinet's two-screen flow rather than inventing one — address,
then a QR code to approve. Read `RommApp/RommApp/Auth/RommClient.swift` in the
Cabinet checkout before writing anything. **Note the HTTP requirement recorded
in Phase 4**: accept a bare host, probe the scheme, never refuse plain HTTP.

**4. More screens.** Home's hero and shelves, the library grid, game detail,
settings. The design system has exact numbers for all of them and the
components exist.

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
- **Configuration is keyed by PLATFORM, not by core.** Genesis Plus GX serves
  four platforms with different option tables and pad types. A `core -> settings`
  map is wrong by construction.
- **Glass does not nest.** One glass layer per modal; everything on it is an
  ordinary surface.
- **Anything a person must read is Callout (31pt) or above.** Caption is for
  glancing at.
- **Design rules are not universal — check which surface they apply to.** "No
  wrapping at the edges" was right for a shelf and wrong for a keyboard, and it
  was written down as a blanket rule before anyone noticed. Consult what
  PlayStation, Xbox and Steam actually do before inventing an interaction; the
  keyboard layout came out of that and is better for it.
- **`dlerror()` clears itself when read.** Read it once.
- **A test must first prove the thing it measures actually varies.** The save
  state test passed while proving nothing, because it compared video on a static
  title screen. It now asserts the picture is moving and reports INCONCLUSIVE
  rather than a difference when there is no audio.

## Two Cabinet-side debts, not CabinetOS's to fix but its problem

1. **Flycast carries unscripted edits in its working tree**, so its pinned commit
   does not reproduce what ships — for Dreamcast and Naomi, and for one of only
   two cores that can answer the parity question cleanly. Capture that diff
   before anything touches the tree.
2. **`core-manifest.json` is not pushed to GitHub yet.** CabinetOS reads it;
   until it lands, the pins live in `cores/build-core.sh` by hand.

## How the user wants this done

Plain answers. Lead with the decision, keep the reasoning in `docs/PROJECT.md`.
Check the running machine before theorising — most wrong turns come from
reasoning off an error message instead of looking. Say plainly what is verified
and what is assumed; he notices and asks. And he pushes back usefully: "are we
sure we can't do X?" has repeatedly produced a better answer than the first one.
