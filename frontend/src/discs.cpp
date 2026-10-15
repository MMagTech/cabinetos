#include "discs.h"

#include <cstdio>

#include "core.h"
#include "disc.h"
#include "storage.h"

// THE NEXT LAUNCH'S DISC, set by Start over on disc N and taken by the
// launch that follows it (#162). Zero: the disc this person was last on.
int gNextStartDisc = 0;
// THE DISC RIG'S TWO KNOBS (#162), for measuring and nothing else:
// --disc-wait, how long Insert keeps the lid open; --disc-boot, Start
// over's method for a system whose right one is being measured (Saturn).
float gDiscWaitSeconds = 2.0f;
int gDiscBootOverride = -1;

// The core's list index that holds disc `n`, or -1.
int discIndexOf(const GameSession& sess, int n) {
    for (size_t i = 0; i < sess.discOrder.size(); ++i)
        if (sess.discOrder[i] == n) return static_cast<int>(i);
    return -1;
}

// WHICH DISC IS IN, as the core says, kept and remembered for this person:
// after a state (PS1's core puts the state's disc in itself) and at the
// end of the game (GameCube changes its own discs).
void syncDiscFromCore(GameSession& sess) {
    // NOT GAMECUBE: Dolphin changes its own discs and its libretro port's
    // index never follows (Boot.cpp, 1a0f972), so nothing here can know.
    if (sess.discs.empty() || sess.discChange == disc::Change::Automatic) return;
    const int idx = cab::Core::shared().discIndex();
    if (idx >= 0 && idx < static_cast<int>(sess.discOrder.size()))
        sess.disc = sess.discOrder[static_cast<size_t>(idx)];
    disc::remember(storage::currentUser(), sess.romId, sess.disc, disc::highest(sess.discs));
}

// A DISC PUT IN AT ONCE, with no time for the game to see the lid open:
// before the first frame (Start over), and before a save state, which
// replaces the whole machine anyway. Insert during play is the frame
// loop's, with the lid open for a while (beginInsert).
void discSwapNow(GameSession& sess, int n, const char* why) {
    cab::Core& core = cab::Core::shared();
    const int idx = discIndexOf(sess, n);
    if (idx < 0) return;
    const bool opened = core.setDiscEjected(true);
    const bool set = core.setDiscIndex(idx);
    const bool closed = core.setDiscEjected(false);
    std::fprintf(stderr,
                 "[disc] disc %d in at once, %s: open %s, index %d %s, close %s; the core "
                 "says index %d\n",
                 n, why, opened ? "yes" : "no (already open?)", idx, set ? "set" : "REFUSED",
                 closed ? "yes" : "no", core.discIndex());
    syncDiscFromCore(sess);
}
