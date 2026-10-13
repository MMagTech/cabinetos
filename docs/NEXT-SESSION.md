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
   (0d8c:0014) that cannot sense a plug, which WirePlumber ranks above HDMI
   (analog 15 / port 99 against HDMI 9 / 59), so sound went to the empty
   jack. **Decided by MMagTech, 2026-10-13:** the TV the picture is on comes
   first; a headphone jack that senses the plug takes over while headphones
   are in and hands back when they come out; nothing else takes the sound
   from the TV for now (not a jack that cannot sense, not USB audio, which
   is undecided, not Bluetooth, a separate future discussion); no setting.
   It must not block adding Bluetooth audio later. Build it with
   WirePlumber's own priority rules. **Success, proven by measurement:**
   on his machine the TV keeps the sound; on the A9 (ALC269VC, which has a
   "Headphone Jack" sensor) headphones in moves the sound to them, and
   unplugging always brings it back to the TV. Start with the baseline:
   MMagTech plugs in his 3.5 mm headphones on today's image while the jack
   sensor and `wpctl` are read live; then build, and repeat several
   plug/unplug cycles, once across TV standby, with a fake higher-ranked
   output standing in for deletableman's chip, and a menu click and a game
   each step. Then draft his reply.

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
