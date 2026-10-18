# Screen looks (#122)

#122 is "Shaders: scanlines, CRT and screen looks (to be designed)".

RetroArch's own GLSL shaders, unchanged, from
https://github.com/libretro/glsl-shaders at commit
`435612fe4f1023117b3aae48c88603fb413404a3`. Only the presets the console
offers and the files they name are copied; paths are kept as upstream has
them, so a preset's relative paths still work.

Each file keeps its own licence, stated in its header where it has one:
crt-lottes, lcd3x and sharp-bilinear-simple are public domain; crt-easymode
and its halation version, crt-geom, zfast-crt, crt-aperture,
crt-guest-dr-venom, zfast-lcd, lcd-grid-v2 and the Game Boy dot-matrix are
GPL. docs/LICENCES.md has each one's author and the files with no header.
They are loaded at run time by `frontend/src/screenfx.cpp`, not compiled
into the frontend.

They are copied by `tools/fetch-shaders.py`, run from the repository root
(it needs `gh`). The looks the console offers are listed in
`frontend/src/looks.cpp`, and the script's `PRESETS` must list the same ones.

To update: change `COMMIT` in `tools/fetch-shaders.py`, delete the copied
folders here (the script keeps any file that is already present), run it,
and change the hash above and in docs/LICENCES.md.
