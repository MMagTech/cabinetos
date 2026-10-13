# Next session

**Finish what is open; start nothing new (MMagTech, 2026-10-13).** Work only on
what is listed under *Next*, in order. Replace this file at the end of every
session; list only what the next session works on.

Every session: `docs/lessons/README.md` (ten rules), then the lessons file
named on the item. New here? `docs/WORKING.md`. The specification is
`docs/PROJECT.md`. Name every issue and pull request by its title as well as
its number, every time.

## Where things stand

**2026-10-13:** `latest` is 2026.10.13. The A9 (Wi-Fi, **192.168.1.109**)
runs `testing` **2026.10.13.5**: the TV-off-at-start fix, judged and passed.
RomM's documentation together with RomM's behaviour is the source of truth
for anything on RomM's data; nothing beyond it is invented.

## Next

1. **Merge the TV-off-at-start fix:** pull request #382, "Start sharp when
   the TV was off at boot, and reach the console before the TV is on"
   (closes #268). Tested in full on the A9 (boot with and without a TV, the
   TV going and coming back, Remote Play and Moonlight, Steam, a game with
   sound). Needs only MMagTech's go to merge; then check the What's new page
   shows 2026.10.13.5, and delete the branch on his go.
2. **Deletableman's missing art:** #376, "Not all art is downloading from my
   RomM instance" (and the art half of #341, "No audio"). Built and tested
   on branch `cover-formats`: every cover format RomM accepts (WebP, AVIF,
   GIF, BMP, TIFF through SDL3_image), the large cover when the small one is
   missing or unreadable, and every failure named in the log. Push to
   `testing`, add its changelog entry, open the pull request, judge the
   image (covers on Home and the grid), then draft a reply asking him to
   update and send a new report. He has RomM's WebP task on.
3. **The grid heading kept in place:** branch `grid-heading-pinned`, judged
   on the TV ("perfect"): the platform, count and selected game's name stay
   at the top, and the grid scrolls by whole rows. Testing image, changelog
   entry, pull request, merge.
4. **Deletableman's no sound:** #341, "No audio". Researched: his
   HC Technology HCAR5000-MI (sold as AM06 Pro) has no analog codec ("no
   codecs found" on 05:00.6); its jack is a built-in C-Media USB chip
   (0d8c:0014; three other HCAR5000-MI probes on linux-hardware.org have
   it), which WirePlumber ranks above HDMI (analog 15 / port 99 against
   HDMI 9 / 59), so sound went to the empty jack ("[pace] sound +0.46%" in
   his first report). The WirePlumber "api.alsa.acp.device" warning is in
   his working report too: not the fault. Steam fixed it only by saving a
   default; no fix may rely on Steam. **Waiting on MMagTech's decision**
   on the rule: the screen the picture is on first; headphones that the
   jack senses, USB and Bluetooth headsets take over; no Settings row. Then
   build, test on the A9 (a fake higher-ranked output), and draft his reply.

Only after those: **same-name games show RomM's tags in the heading**
(agreed; #383, "Game versions, updates and DLC: one place to choose
them", which also parks the full design).

Waiting, not for this session unless MMagTech raises them: #329, "release
notes on the console" (his Discord group's answer about a QR code); #380,
"The console follows any web address in RomM's data, with the RomM sign-in
attached"; #381, "Remote Play: stream with no screen connected".

**After a console is installed fresh** it has no key: About, press Version
seven times, Developer access, PIN; then from the Mac, with the password on
the TV, `ssh-copy-id -i ~/.ssh/cabinetos.pub -p 2222 cabinet@<address>`.
Password logins and sudo are MMagTech's to type, never the assistant's.

Never run a headless frontend on the A9 while MMagTech is at the TV: it reads
the same controllers. Never press anything on the A9 he did not ask for.
