# Every Windows arcade game goes in one RomM folder, windows-arcade

- **Decided:** One RomM folder, `windows-arcade`, holds every Windows arcade game, whatever its board. On the console it joins the Arcade tile. Each game is a zip or 7z of its folder with its `<gameid>.windowsloader` file inside (an unpacked folder still plays), and comes down as one file, as a Lindbergh game does.
- **Date:** 2026-11-26 (MMagTech, #762)
- **Why:** The loader knows each game's board from its ID, so a folder per board would tell the console nothing and would ask the library's owner to sort by board. RomM has a name for only one of these boards, Type X; a folder it does not know becomes a platform of its own, whose covers are added by hand. The console knows the folder by RomM's name for the platform, so the folder keeps its own. These games hold up to 12,252 files, and RomM is slow with a folder of thousands.
- **Numbers:** #762, "Windows Arcade: games from Windows-based arcade boards, in windowsarcadeloader".
- **Replaces:** none.
