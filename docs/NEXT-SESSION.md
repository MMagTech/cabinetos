# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

**2026-10-12: #351 (volume keys, PR #358) and #352 (smart collections, PR
#359) are built, tested on the A9 through the loop, and on `testing`
together** (#359 is stacked on #358). The A9 (Wi-Fi, **192.168.1.109**,
`CABINETOS_A9=cabinet@192.168.1.109`) runs that image, `testing`
2026.10.12.2, checked from its log; nothing on it by hand.
Cabinet on Apple platforms is no longer a constraint; a CabinetOS change that
leaves Cabinet behind gets a record issue on MMagTech/cabinet.

## Next

1. **Finish #351 and #352** (MMagTech's checks, then his merge word):
   - The image is proven from the A9 (version, no drop-ins, the key rule
     from `/usr`, `[volume] full at start`, `[library] 6 collection(s), 4
     of them smart`, an injected press 1.00 to 0.95 and back). List what it
     holds and the checks he may want; he picks.
   - Merge #358 first, then #359 (its diff is then its own commit). Close
     both issues with a comment; reply on #351 asking Spinnich to check the
     rocker on the Deck once it is in an image (draft it, post on his go).
   - Delete the four `Test:` smart collections on RomM on his go (listed on
     #352; `~/.config/cabinetos/romm-token-collections.json`), and the merged
     branches.
2. **#268 (PR #332, on hold):** the display wait works, but the app starts
   Developer access, Remote Play and Tailscale, so with no display none came
   up. Rework: start them at boot from their saved state, independent of the
   session; then retest with the HDMI unplugged (MMagTech at the TV). Branch
   `wait-for-display`.

Waiting, not for this session unless MMagTech raises them: #360 (an
update says "No connection" when only the router's DNS fails), #353, #354, #355
(saves that never sync: Saturn cartridge, arcade high scores, cartridge
clocks; After first release), #347 (pads did not reconnect after an update
reboot), #345 (PS1 DualShock), #349 (to discuss: rolling back an update).
Upstream to watch: rommapp/romm#5175 and #5198 (save channels),
rommapp/argosy-sigil#17 and #18 (our Xbox and Xbox 360 notes),
flyinghead/flycast#2541, libretro/Genesis-Plus-GX#413.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
