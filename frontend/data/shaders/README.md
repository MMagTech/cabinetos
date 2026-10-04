# Screen looks (#122)

RetroArch's own GLSL shaders, unchanged, from
https://github.com/libretro/glsl-shaders at commit
`435612fe4f1023117b3aae48c88603fb413404a3`. Only the presets the console
offers and the files they name are copied; paths are kept as upstream has
them, so a preset's relative paths still work.

Each file keeps its own licence, stated in its header: crt-lottes, lcd3x and
sharp-bilinear-simple are public domain; crt-easymode, crt-geom, zfast-crt,
crt-aperture, zfast-lcd, lcd-grid-v2 and the Game Boy dot-matrix are
GPL. They are loaded at run time by `frontend/src/screenfx.cpp`, not compiled
into the frontend.

To update: fetch the same paths at a newer commit and change the hash above.
