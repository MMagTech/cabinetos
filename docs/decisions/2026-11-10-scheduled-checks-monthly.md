# The controller list and Steam's spare are checked monthly

- **Decided:** The controller list and the stored spare of Steam's session are checked on the 1st of each month, the same day as the Bazzite base check, instead of every Monday. Each still only opens a pull request and never merges.
- **Date:** 2026-11-10
- **Why:** New controllers appear slowly, and the spare is used only when terra fails during an image build, so a week's freshness bought nothing a player sees while each pull request cost a review and a place in an update. On one day, all three land together and ride the same monthly update. The cost: a brand-new controller may wait up to a month longer to be recognised.
- **Numbers:** #66, "Settings: button mapping"; #270, "Image build: fall back to a stored copy of Steam's session when Terra fails".
- **Replaces:** the weekly pull request in [no-mapping-screen](2026-09-26-no-mapping-screen.md) (2026-09-26), and the weekly schedule of the spare's check (#270).
