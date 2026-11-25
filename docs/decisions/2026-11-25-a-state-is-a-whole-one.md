# Every core is told a state is a whole one

- **Decided:** A core that asks what a state is for (`RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT`) is told "normal", a whole state, every time: one made by Save state, one made by Rewind, on every core. A CPS3 state made before this is refused ("Couldn't load that state"), and nothing is built for those.
- **Date:** 2026-11-25
- **Why:** Of the image's cores only FinalBurn Neo and Snes9x ask, and Snes9x does the same with that answer as with none. FinalBurn Neo took the silence for online play: it kept no high scores, and left 8 MB of graphics memory out of a CPS3 state, so every CPS3 state made until now loads scrambled at another start (seen in Red Earth; measurements on #608). Telling it Rewind's states are ones the same run loads again, the answer RetroArch gives for running ahead, would change nothing it keeps and would make what a core leaves out depend on who asked. An old CPS3 state was broken already, so a check that turns it away more neatly, or a conversion, has nothing to save.
- **Numbers:** #608, "FinalBurn Neo games keep no high scores"; PR #751, "FinalBurn Neo games keep high scores, and CPS3 save states load clean (#608)".
- **Replaces:** none.
