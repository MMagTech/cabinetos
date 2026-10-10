# Next session

**Work only on what is listed under *Next*. Do not raise other work, cleanup
or old findings with MMagTech; everything else is a GitHub issue in a
milestone, and it waits its turn.** Replace this file at the end of every
session; list only the current milestone's work, never housekeeping.

Every session: `docs/lessons/README.md` (ten rules, one page), then the
lessons file named on the item. New here? `docs/WORKING.md`. The
specification is `docs/PROJECT.md`; the order is `docs/ROADMAP.md`.

## Where things stand

2026-10-09: #304 is merged (#306): a stream into Steam keeps its picture.
CabinetOS now builds Sunshine itself with one patch (`cores/build-sunshine.sh`,
`cores/sunshine-patches/`), offered upstream as LizardByte/Sunshine#5885 (sent
by MMagTech from his fork; watch it, draft replies for him to post). The site
is live at https://mmagtech.github.io/cabinetos/ (showcase page, wiki, README;
#307, #309). The A9 (Wi-Fi, **192.168.1.109**,
`CABINETOS_A9=cabinet@192.168.1.109`) runs testing image 2026.10.09.5; main has
since built the image with the fix for an idle-ended stream leaving the TV's
controllers set aside, for MMagTech to update to from System update (the PIN
is his to enter; never sudo over SSH).

## Next

Milestone 7, Ready to ship (docs/ROADMAP.md), in this order. Lessons file:
image-and-ci.md.

1. **Signed images (#135), started 2026-10-09 on branch `signed-images`**
   (pushed, NOT yet on `testing`). Done: MMagTech made the key pair; the
   secret half is the `SIGNING_SECRET` repository secret (set 23:16) and his
   password manager; the public half is in the branch at
   `system_files/etc/pki/containers/cabinetos.pub`. The branch adds the
   policy.json rule (`build_files/require-signed-updates.sh`, dry-run on the
   A9 passed), registries.d, `--enforce-container-sigpolicy` in the
   installer, and "Not signed by CabinetOS" as an update failure reason. He
   is the only user: no migration code, his A9 switches once by a command he
   types (sudo is his). Next: push to `testing` (the first signed build),
   check the policy with skopeo in a container (signed tag accepted, an old
   unsigned digest refused), then he updates and runs the one switch, then a
   few updates to prove it. Signing comes before any release ISO.
2. **Arcade games in any RomM arcade folder (#310).** Today only folders
   named exactly `FBNEO` or `MAME2003` show. His own setup (`FBNEO` named
   "Arcade", `MAME2003` named "Lightgun" in RomM) must keep working.
3. **A diagnostic report (#195).**
4. **Licences checked and the full texts shipped (#120).**
5. **At the first release (#308):** the installer on a GitHub Release, and the
   page, wiki and README pointing at it.

Also in the milestone: booting with the TV off (#268). After first release:
#290, #283, #303, #305 (opening Steam sometimes takes 25 s; not worth time
now). Raised and not filed: the console reads RomM's library only at start,
on switching person and on coming back online, so a renamed platform or new
games show only after a restart; he may want it filed.

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers (docs/lessons/testing.md). Never press anything on the
A9 he did not ask for while he may be using it.
