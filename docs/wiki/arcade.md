# Arcade

How to set out arcade games in RomM for CabinetOS, and what each one gets.

- **One folder for each system**, named the way RomM knows it. All of them are one Library tile, **Arcade**, in RomM's title order. The line beside its heading names the focused game's folder: *Metal Slug · NeoGeoMVS*. Everywhere else an arcade game's system is *Arcade*. A game in two folders shows twice. For one board on its own, make a RomM collection of it.

  | Folder | Games |
  |---|---|
  | `cps1`, `cps2`, `cps3` | Capcom CPS1, CPS2 and CPS3 |
  | `neogeomvs` | Neo Geo |
  | `naomi`, `naomi2`, `naomigd`, `atomiswave` | Sega Naomi, Naomi 2 and Naomi GD-ROM, and Sammy Atomiswave |
  | `stv` | Sega ST-V |
  | `model2`, `model3` | Sega Model 2 and Model 3 |
  | `chihiro` | Sega Chihiro |
  | `lindbergh` | Sega Lindbergh |
  | `windows-arcade` | Windows Arcade: games from Windows-based arcade boards |
  | `fbneo` | FinalBurn Neo sets |
  | `gaelco` | Gaelco |
  | `arcade` or `mame` | everything else |

- **Sets:** non-merged, for MAME 0.289 or MAME 2003-Plus (0.78), as zip or 7z. MAME 0.289 sets also play in FinalBurn Neo, Flycast, sm2-emu and Supermodel. MAME 2003-Plus and Supermodel play only zip, so keep Model 3 games as zip.
- **Keep each set's own name**, for example `sf2.zip`. The name and the files inside it pick the emulator.
- **A game with a disc is one folder** with the zip and its CHD side by side, for example `cps3/sfiii3/sfiii3.zip` and `cps3/sfiii3/cap-33s-2.chd`. Use the CHD that MAME 0.289 lists. A CHD in a subfolder is not found.
- **CPS3:** a CD set, a small zip and a CHD, installs the game into the board the first time each person plays it, about 22 minutes: choose *Rewrite the game* with button 1. The No CD sets, whose names end in `n` (`sfiii3n`), start at once.
- **BIOS:** Naomi, Naomi 2, Naomi GD-ROM and Atomiswave need their BIOS zips as RomM firmware for that folder: `naomi.zip`, `naomi2.zip`, `naomigd.zip` and `awbios.zip`. Chihiro needs `chihiro.zip` and `seganetw.zip`, MAME's, as they are. Other arcade games carry theirs inside each zip.
- **Greyed:** a game no emulator here plays, one its emulator marks as not working, or one made for anything but a controller (a touchscreen, a mahjong or hanafuda panel, a gambling, medal or electromechanical machine) shows *Not supported*. Light-gun games show *Needs a light gun*. A zip whose files do not match its set says so when you press Play.
- **Hold Start for a second for the game's own settings.** In a MAME game this opens MAME's menu, where **Dip Switches** has the difficulty, the lives and the rest. Up and down move, left and right change a setting. If the menu ends with **Return to Game**, **B** chooses and **A** goes back; if it ends with **Close Menu**, **A** chooses and **B** goes back. Hold Start again, or choose that last row, to play on. In a FinalBurn Neo, Naomi or Atomiswave game it opens the board's own test menu, where the board has one. Some games have none, Final Fight among them: holding Start does nothing there. What you change in a MAME's menu is kept on this console, for you; in a board's test menu it is part of the game's save.
- **Buttons:** the 8BitDo Arcade Stick plays arcade games with the cabinet's layout ([Fight sticks](controllers.md#fight-sticks)), and **Controls** in the pause menu moves a game's buttons for any controller ([Controls](pause-menu.md#controls)).
- **Model 2, Model 3, Chihiro, Lindbergh and Windows Arcade games** run in emulators of their own, so they have less: their pause menu has no save states and holding Start does nothing. A button moved in **Controls** in a Model 2 or Model 3 game works from the game's next start.
- **High scores are part of the game's save**, yours alone, and go to RomM with it when you leave, so your other consoles have them too. So does all a board remembers, and a player's card in a Naomi, Chihiro or Lindbergh game that uses one. A FinalBurn Neo game keeps its scores when FinalBurn Neo's own list has the game, as it has nearly all of CPS1 and CPS2 and most others, or when its board keeps them, as Neo Geo boards do. Two things stay on the console: what you set in a MAME's menu, and what a MAME game writes to its hard disk, so a game that keeps its scores there keeps them on that console.

## Chihiro

- **Each game is one file, `<set>.bin`**, for example `outr2.bin`: the game's disc and its key, from MAME's zip for the set, joined once into one file with [chihiro-netboot](https://github.com/Tovarichtch/chihiro-netboot), as on Batocera. The console never converts a game. A disc or a zip in the Chihiro folder does not play, and a `.bin` that is not the game its name says shows *outr2.bin doesn't match its set* when you press Play.
- **BIOS:** `chihiro.zip` and `seganetw.zip`, MAME's, as RomM firmware for the Chihiro folder. Without both, a game says *No Chihiro BIOS on the server*.
- **What plays:** OutRun 2, OutRun 2 Special Tours, Crazy Taxi High Roller, Wangan Midnight Maximum Tune 1 and 2, Ollie King and Gundam Battle Operating Simulator, each in all its versions. The House of the Dead III, Virtua Cop 3 and Ghost Squad show *Needs a light gun*; the mahjong, golf and card-terminal games, and the update discs, show *Not supported*.
- **Controls:** steer with the left stick or the d-pad; the right trigger accelerates and the left brakes; the right and left shoulders change gear up and down (Crazy Taxi: Drive and Reverse; Maximum Tune moves its lever a gear at a time); **A** jumps in Crazy Taxi; **X** changes the view; **Y** puts in a card in Maximum Tune and Gundam. Ollie King's board is the left stick, its grabs the shoulders. Gundam's two sticks are the pad's two, their triggers the triggers, their buttons the shoulders, the pedal **A**. Select puts in a coin, Start starts; some games take two coins, OutRun 2 among them.
- **A player's card** is made the first time you put one in, and is part of your save from then on.

## Lindbergh

Sega Lindbergh games play in Linux Loader, as an experiment: some games may run into problems.

- **Put each game in your `lindbergh` folder as a zip or 7z of its folder.** Take `drv/nvidia` out of a TeknoParrot copy first. A DVD image (`.iso`, `.chd`) does not play.
- **What plays:** pad games such as Hummer, OutRun 2 SP SDX, Initial D, Virtua Fighter 5 and After Burner Climax. Gun games show *Needs a light gun*. Mahjong and quiz games, and versions Linux Loader doesn't know, show *Not supported*.
- **Controls:** Select puts in a coin. Driving: the left stick steers, the triggers accelerate and brake, **A** boosts, the shoulders change gear, **Y** changes the view. After Burner: the left stick flies, the triggers set the speed, **A** fires the gun, **B** the missiles, **Y** the Climax.
- **The first start is slow:** the whole game downloads and unpacks, and needs room for it twice over.
- **If a game stops on an error** (Hummer's *Error 1011*), choose Exit to Home and start it again.

## Windows Arcade

**Windows Arcade is very experimental.** Some games will not start, and some will start and not play right. CabinetOS runs these games with [windowsarcadeloader](https://github.com/aderumier/windowsarcadeloader), a separate project, and each game works or not according to that project. CabinetOS cannot fix a game that doesn't work in it. Its page lists every game it knows and what each one needs.

- **What plays:** the games windowsarcadeloader marks as working, on a controller. Gun games show *Needs a light gun* until light guns play ([#575](https://github.com/MMagTech/cabinetos/issues/575)). Games it does not mark as working, games made for anything but a controller (a touch screen, a trackball, a mahjong panel, a medal machine), and a game without its `.windowsloader` file show *Not supported*.
- **All of them go in one folder, `windows-arcade`**, whatever board they come from, each as a zip or 7z of the game's folder (the folder itself plays too). RomM does not know this folder, so it has no covers for these games: add them by hand in RomM. Keep the folder's name as it is: the console knows these games by it.

### Setting it up, for the library's owner

You do this on your own computer, a Mac, Linux, or Windows. Nothing from Windows runs on it: a script of CabinetOS's downloads files, checks them, packs them, and writes one small file into each game.

**Once:**

1. On Windows only: open PowerShell as administrator, type `wsl --install`, restart, then open **Ubuntu** from the Start menu. Do every step below in that window.
2. Download the script:
   `curl -LO https://raw.githubusercontent.com/MMagTech/cabinetos/main/tools/windows-arcade-admin.sh`
3. Make it runnable: `chmod +x windows-arcade-admin.sh`
4. Get the Windows files: `./windows-arcade-admin.sh files`. It downloads about 615 MB from Microsoft and a few other places, checks every file, and makes `windows-arcade-files.tar`. It takes a few minutes.
5. If it says a file could not be downloaded, open the address it shows in your browser, put the file in the folder it names, and run step 4 again.
6. In RomM, add `windows-arcade-files.tar` as firmware for the `windows-arcade` folder, as you do for the other arcade BIOS files.

**For each game:**

7. Run `./windows-arcade-admin.sh games` followed by the game's folder, zip or 7z. You can drag it into the window to type its path. On Windows, your drive C: is `/mnt/c`, so your Downloads folder is `/mnt/c/Users/<your name>/Downloads`. A folder holding several games does them one after another.
8. It shows the games whose names are closest. Press Enter if the first is right, type its number if another is, or type words from the game's name to search again.
9. It shows the game's programs, the one it thinks starts the game first. Press Enter, or type another number.
10. Put the game in RomM's `windows-arcade` folder as it is (folder, zip or 7z), then scan.

If the script says it needs a program it does not find (`zip`, `unzip`, or 7-Zip for a 7z), install that program and run it again. Adding the file to a zip takes about a second, whatever its size; a 7z is copied once, so it needs free room as big as itself.

**After a CabinetOS update,** a game may say *The Windows files on the server are out of date*. Download the script again, run step 4 again, and put the new `windows-arcade-files.tar` in RomM in place of the old one.

### Playing

- **The first start is slow:** the whole game downloads and unpacks, and needs room for it twice over. The first Windows Arcade game you start also sets up Windows on the console, once, which takes a few minutes: the game's page says *Setting up Windows…*. If it says *No Windows files on the server*, the file from step 6 is not in RomM.
- **Controls:** Select puts in a coin, Start starts, and each game's other buttons are windowsarcadeloader's for that game. Hold **R3** (press the right stick) for the cabinet's Test switch, which opens a game's operator settings where it has them, and hold **L3** for its Service switch. Holding Start does not open settings here.
- **Saves:** what a game keeps, its scores, a board's memory and its operator settings, is each person's own save, and goes to RomM when you leave, so your other consoles have it too. There are no save states and no RetroAchievements.
- **Picture** is in the pause menu only for a game that offers a picture size; the others draw at their own size.
