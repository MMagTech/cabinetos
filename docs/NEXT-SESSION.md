# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

Milestone 3 is under way on branch `fast-cores` (local, not pushed). The A9
runs its image, on `balanced`, nothing deployed by hand. **Never tell
MMagTech something is unrecoverable or safe to delete before every cause is
checked** (memory).

Measured 2026-10-01, written up on the issues:
- **#163:** amd_pstate's `prefcore` reads disabled because AMD's HFI driver
  owns the ranking, and the scheduler has it. A paced game still lands on
  the slow cores (Dolphin 96%), costing 20 to 36% more work per frame.
  Built: `frontend/src/cpus.{h,cpp}`, games inside the console on the fast
  cores; Dolphin and Flycast measured 100% there. Separate emulators
  untouched (proc children start on every core).
- **#150:** no consistent difference at 1x, 3x or 4K on GameCube; the noise
  was #163. The heavy separate emulators are not measured yet.

## Next: one TV session with MMagTech (about 20 minutes, agreed)

Plan as given to him; tell him the steps again and wait for "go".
Deploy the branch with `tools/ui-loop.sh`, restore with `--restore` after.

1. GameCube Burnout, a minute, Exit to Home: plays as normal.
2. PS2 Burnout 3, a minute, Exit to Home. Check with
   `~/perf150/where.py <pid> 30` that PCSX2's threads are on the fast cores
   (the `--core/--rom` path does not boot PS2 headless, so this is the only
   check) and `[cpus] back on every core` in the journal after.
3. Mario Kart 8 Deluxe, three races on one track (Time Trial): as today,
   then `sudo tuned-adm profile throughput-performance-bazzite`, then back
   to `balanced` and `taskset -a -c -p 0-3,12-15 <eden pid>`. Log each race
   with `~/perf150/frames.py 120 <label>` (gamescope's frame times, read
   from the mangoapp message queue nobody else reads) plus `where.py`.
   Restore `balanced` after.

Then: decide #150 and whether separate emulators get the fast cores, with
MMagTech; testing push and his judgement before any merge.

Also open, not in the way:
- **#200**, real Wii Remotes, when the hardware arrives; one TV session.
- **#204**, Dolphin's log level, one line, with the next testing push.
- Then **#63 phase 1**: the dial, the fixed tables, #209.
