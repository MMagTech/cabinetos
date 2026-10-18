# The disc rig (#162)

#162 is "Multi-disc games: Change disc in the pause menu, one save per game
(Saturn first)".

Games on several discs, tested on the A9 with nobody at the TV: the console's
own frontend, headless, against a scratch console root, with every request to
RomM through `tools/romm-readonly-relay.py` (reads only; any write gets 403;
its server comes from `CABINETOS_RELAY_UP`).
Nothing reaches RomM and nothing touches the real console's drive or Recent.
**Never while MMagTech is at the TV**: a headless frontend reads the same pads.

## Set up, once per boot

`<rig>` below is the rig's scratch folder on the reference console, any
folder: the scripts use the one they are in.

```
# on the reference console, from a frontend built in <rig>/frontend (lessons: testing.md)
mkdir -p <rig>/root/config && cp /var/lib/cabinetos/config/*.json <rig>/root/config/
echo '[]' > <rig>/root/config/drives.json
printf '{\n  "address":"127.0.0.1:16006"\n}\n' > <rig>/root/config/server.json
cp tools/romm-readonly-relay.py <rig>/relay.py      # from the Mac, with scp
CABINETOS_RELAY_UP=http://<your RomM server>:<port> nohup python3 <rig>/relay.py 16006 > <rig>/relay.log 2>&1 &
```

`run.sh`, `regression.sh`, `tile.sh` and `relay.sh` go to `<rig>/` beside it.

## Run

```
<rig>/run.sh <name> <romId> "<step>; <step>; ..." [frontend switches]
<rig>/regression.sh                     # one short pass per system
<rig>/tile.sh /tmp/m.png 3 /tmp/disc-a.png /tmp/disc-b.png ...   # contact sheet
```

`ROMM=127.0.0.1:16099` is the server away; `ROOT=` another scratch root;
`KEEPDISC=1` keeps the remembered discs (otherwise each run starts clean).
A second relay for odd names or a disc missing on RomM:
`CABINETOS_RELAY_UP=http://<your RomM server>:<port> <rig>/relay.sh 16008 hide:38863`
or `file:38873=daedalus1.chd`, then `ROMM=127.0.0.1:16008`.

## Steps (`--disc-script`)

The flag is read in `frontend/src/cli.cpp`; the steps are parsed and run in
`frontend/src/discrig.cpp`.

| Step | Does |
|---|---|
| `wait N` | N of the game's own frames |
| `sleep S` | S wall seconds (for a menu, where the game is paused) |
| `insert N` | what Insert it does: lid open, the game runs, disc N, lid closed |
| `startover N` | what Start over on it does: out of the game and back in on disc N |
| `play <romId>` | out of this game, into another, in the same run |
| `state` / `load` / `resume` | Save state, Load latest state, and A on *Press (A) to continue* |
| `press <button> <frames>` | holds a RetroPad button for the game (`a b x y start select up down left right l r l2 r2`) |
| `shot NAME` | the game's frame as `/tmp/disc-NAME.png` |
| `ui NAME` | the whole screen, menus and all, as `/tmp/disc-NAME.bmp` |
| `menu` / `activate` / `nav up\|down\|a\|b` / `close` | the pause menu and the disc panel, by the same calls a pad makes |
| `check` | logs the disc in the drive, the core's index and lid, the remembered disc |
| `quit` | Exit to Home |

`--disc-wait S` changes how long Insert keeps the lid open; `--disc-boot
list|swap|initial` overrides Start over's method for a measurement.
`--disc-test` checks the naming and ordering rules with no core or server.
