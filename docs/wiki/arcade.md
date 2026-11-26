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
- **Model 2, Model 3, Chihiro and Lindbergh games** run in emulators of their own, so they have less: their pause menu has no save states and holding Start does nothing. A button moved in **Controls** in a Model 2 or Model 3 game works from the game's next start.
- **High scores are part of the game's save**, yours alone, and go to RomM with it when you leave, so your other consoles have them too. So does all a board remembers, and a player's card in a Naomi, Chihiro or Lindbergh game that uses one. A FinalBurn Neo game keeps its scores when FinalBurn Neo's own list has the game, as it has nearly all of CPS1 and CPS2 and most others, or when its board keeps them, as Neo Geo boards do. Two things stay on the console: what you set in a MAME's menu, and what a MAME game writes to its hard disk, so a game that keeps its scores there keeps them on that console.

## Chihiro

- **Each game is one file, `<set>.bin`**, for example `outr2.bin`: the game's disc and its key, from MAME's zip for the set, joined once into one file with [chihiro-netboot](https://github.com/Tovarichtch/chihiro-netboot), as on Batocera. The console never converts a game. A disc or a zip in the Chihiro folder does not play, and a `.bin` that is not the game its name says shows *outr2.bin doesn't match its set* when you press Play.
- **BIOS:** `chihiro.zip` and `seganetw.zip`, MAME's, as RomM firmware for the Chihiro folder. Without both, a game says *No Chihiro BIOS on the server*.
- **What plays:** OutRun 2, OutRun 2 Special Tours, Crazy Taxi High Roller, Wangan Midnight Maximum Tune 1 and 2, Ollie King and Gundam Battle Operating Simulator, each in all its versions. The House of the Dead III, Virtua Cop 3 and Ghost Squad show *Needs a light gun*; the mahjong, golf and card-terminal games, and the update discs, show *Not supported*.
- **Controls:** steer with the left stick or the d-pad; the right trigger accelerates and the left brakes; the right and left shoulders change gear up and down (Crazy Taxi: Drive and Reverse; Maximum Tune moves its lever a gear at a time); **A** jumps in Crazy Taxi; **X** changes the view; **Y** puts in a card in Maximum Tune and Gundam. Ollie King's board is the left stick, its grabs the shoulders. Gundam's two sticks are the pad's two, their triggers the triggers, their buttons the shoulders, the pedal **A**. Select puts in a coin, Start starts; some games take two coins, OutRun 2 among them.
- **A player's card** is made the first time you put one in, and is part of your save from then on.

## Lindbergh

Sega Lindbergh games play in Linux Loader. **This is an experiment: some games may run into problems.**

- **Each game is its hard drive's copy**, the game's whole folder as it ran on the cabinet, for example `Hummer Extreme/` with `elf/`, `data/` and the rest inside. Put it in your `lindbergh` folder **as a zip or 7z of that folder**, one file a game. An unpacked folder plays too, but RomM lists every file in it, so a large one starts slowly. A copy made for TeknoParrot works as it is, but take its `drv/nvidia` folder out first if it has one: Hummer Extreme's holds about 110,000 driver files the game never uses, too many for RomM to list. A DVD image of the game (`.iso`, `.chd`) does not play: the disc is locked, and only the hard drive's copy is open.
- **The console knows a game by the program inside it**, not by its folder's name: a version Linux Loader does not know shows *Not supported* when you press Play.
- **What plays:** the games played on a pad: Hummer and Hummer Extreme, After Burner Climax, OutRun 2 SP SDX, Initial D Arcade Stage 4 and 5, Harley-Davidson: King of the Road, R-Tuned, Sega Race TV, Virtua Fighter 5 and Virtua Tennis 3, each in the versions Linux Loader knows. The House of the Dead 4 and EX, Let's Go Jungle, Rambo, Ghost Squad Evolution, 2 Spicy and Primeval Hunt show *Needs a light gun*; the mahjong and quiz games show *Not supported*.
- **Controls:** Back puts in a coin, Start starts. In a driving game the left stick steers, the right trigger accelerates and the left brakes, **A** boosts, **Y** changes the view and the shoulders change gear. In After Burner Climax the left stick flies, the right trigger speeds up and the left slows down, **A** fires the gun, **B** the missiles and **Y** sets off the Climax. In a fighting or sports game the d-pad or stick moves and **A**, **B** and **X** are the buttons.
- **The first start of a game takes a while:** it downloads the whole game, and some games prepare their pictures before they begin (Rambo for a few minutes the first time). While it unpacks, the console needs room for the game twice over.
- **Some games stop with a message of their own**, Hummer Extreme's *Error 1011* now and then among them: choose **Exit to Home** in the pause menu and start it again.
