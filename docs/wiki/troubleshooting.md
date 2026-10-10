# When something is wrong

## Messages a game can show

The system's name in a message is RomM's name for the platform.

| Message | What to do |
|---|---|
| *No Sega Saturn BIOS on your server* (or another system) | Add the BIOS to RomM as firmware for that platform. File names: [Systems, formats and BIOS](systems.md). |
| *No Nintendo Switch keys on your server* | Add `prod.keys` as Switch firmware in RomM. |
| *Needs newer Nintendo Switch keys on your server* | The game needs keys from a newer firmware. Replace `prod.keys`. |
| *No PlayStation 3 firmware on your server* | Add `PS3UPDAT.PUP` as PS3 firmware. |
| *No license for this game on your server* | Put the game's `.rap` file in the same RomM game folder as its PKG. |
| *No Xbox BIOS on your server* | Add the MCPX boot ROM and a flash BIOS as Xbox firmware. |
| *This game's file is a web page, not a game* | The file on the server is a saved download page. Replace it in RomM. |
| *Needs X.X GB more space* | Remove downloads (Settings, Storage, Downloads) or plug in a drive. |
| *Couldn't start this game* | Check the format against [Systems](systems.md#file-formats-in-short). A zipped Dreamcast, GameCube, Wii or Saturn disc will not start: use CHD, RVZ or ISO. |

## The Library

- **A system is missing.** Its RomM platform slug must match the
  [list](systems.md#every-system). Arcade folders must be named `FBNEO` or
  `MAME2003` exactly.
- **A tile says *Emulator not installed*.** Switch and Xbox emulators install
  from Flathub on the first boot with a network, and try again every six hours.
  Leave the console on and connected.
- **A Wii or Wii U game is greyed.** It needs a Wii Remote (pair one) or the Wii
  U GamePad (not supported).
- **The top bar says *Offline*.** The server cannot be reached. Games on the
  drive still play, and saves wait. See [Offline](library.md#offline).

## Controllers

- **A controller will not pair.** The console pairs without a passkey. Put the
  controller in pairing mode again, and keep it close.
- **A Wii Remote will not pair.** Press the red sync button inside the battery
  cover, not the 1 and 2 buttons.
- **No pause menu with the keyboard.** In PS2 and in Switch, PS3, Xbox, Xbox 360
  and Wii U games, use a pad: both sticks pressed together.

## The screen

- **The TV is black.** The console turned the screen off after the time in
  Settings, Display and Sound. Press any button; that first press only wakes
  it.
- **A multi-disc game stops at the disc change.** Changing discs is not built
  yet.

## Remote Play

- **Steam looks washed out on the device.** Turn on HDR in Moonlight's settings.
- **The device cannot find the console.** Check Streaming is on, and that the
  device is on the same network (or on Tailscale, using the Tailscale address).

## Reporting a problem

1. Settings, **System**, **Create diagnostic report**. Scan the code with a
   phone on the same network to download the report, or type the address
   shown under it into a computer. It is also in
   [File access](file-access.md), under `reports`.
2. [Open an issue](https://github.com/MMagTech/cabinetos/issues/new/choose)
   and attach the report. Say what you saw and when.

The report says which machine it is, every version, the settings and the
console's logs. Your server's address, names, Wi-Fi networks, network
addresses, serial numbers and passwords are replaced before it is written, so
it is safe to post. Game titles stay in.

If the console does not get as far as Settings, a report from the next boot
still has the boot that failed. With Developer access, `cabinetos-report`
makes the same file.

CabinetOS is tested on the GEEKOM A9 Max only, so reports from other machines
are how it gets better on them.
