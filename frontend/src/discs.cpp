#include "discs.h"

#include <cstdio>

#include "app.h"
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

// ---- Change disc in play (#162, #398) --------------------------------------
// Members of the App (app.h), moved out of main.cpp in batch (f).

// ---- Change disc (#162) ----------------------------------------------
//
// ONE ROW IN THE PAUSE MENU, ONLY ON A GAME WITH SEVERAL DISCS, and
// everything else inside the question panel: every disc listed, earlier
// ones too, the one in the drive marked, focus on the next. Picking one
// asks Insert (the game keeps running) or Insert and restart.
//
// INSERT IS EJECT, LET THE GAME RUN, SET THE DISC, CLOSE: RetroArch
// waits 100 frames, and a Dreamcast game needs the tray seen open
// (flycast#1914). The menu closes at once so the wait runs in the game;
// a core that is paused never sees the lid (beetle-psx#694).
void App::beginInsert(int n) {
    if (session.discs.empty() || session.discInserting) return;
    cab::Core& core = cab::Core::shared();
    const bool opened = core.setDiscEjected(true);
    const double fps = core.avInfo().fps > 0 ? core.avInfo().fps : 60.0;
    session.discInserting = n;
    session.discCloseAt =
        core.framesRun() + static_cast<uint64_t>(std::max(1.0, fps * gDiscWaitSeconds));
    std::fprintf(stderr, "[disc] insert disc %d: lid open %s, closing in %.1fs of play\n", n,
                 opened ? "yes" : "no (already open?)", gDiscWaitSeconds);
    menuNotice.say("Changing disc\xE2\x80\xA6", Tone::Busy);
}

void App::pumpInsert() {
    if (!playing || !session.discInserting) return;
    cab::Core& core = cab::Core::shared();
    if (core.framesRun() < session.discCloseAt) return;
    const int n = session.discInserting;
    const int idx = discIndexOf(session, n);
    const bool set = core.setDiscIndex(idx);
    const bool closed = core.setDiscEjected(false);
    session.discInserting = 0;
    syncDiscFromCore(session);
    // A REWIND across a disc change would put the machine back on a disc
    // that is no longer in it.
    rewindKeep.reset();
    rewindLastFrame = core.framesRun();
    std::fprintf(stderr, "[disc] disc %d in: index %d %s, lid closed %s, the core says "
                         "index %d\n",
                 n, idx, set ? "set" : "REFUSED", closed ? "yes" : "no", core.discIndex());
    if (set && session.disc == n)
        menuNotice.say("Disc " + std::to_string(n) + " in", Tone::Done);
    else
        menuNotice.say("Couldn't change the disc", Tone::Problem);
}

void App::startOverOnDisc(int n) {
    if (session.discs.empty()) return;
    std::fprintf(stderr, "[disc] start over on disc %d\n", n);
    disc::remember(storage::currentUser(), session.romId, n, disc::highest(session.discs));
    gNextStartDisc = n;
    relaunchRomId = session.romId;
    if (choiceScreen.isOpen()) choiceScreen.close();
    exitToHome();
    // The exit just remembered the disc that was in; this one wins.
    disc::remember(storage::currentUser(), relaunchRomId, n, disc::highest(session.discs));
}

void App::pickDisc(int n) {
    // INSERT ONLY WHERE THE CORE HAS A DRIVE, and not for the disc
    // already in it.
    const bool canInsert =
        session.discChange == disc::Change::Insert && n != session.disc;
    // ONE THING TO DO IS SAID, NOT FOCUSED: a lone button nobody moved
    // onto does not read as selected.
    std::vector<std::string> options;
    if (canInsert) {
        options.push_back("Insert");
        options.push_back("Insert and restart");
    }
    const disc::Disc* picked = disc::byNumber(session.discs, n);
    askChoice(picked ? picked->label : "Disc " + std::to_string(n), "", options, 0,
              [&, n, canInsert](int answer) {
                  if (canInsert && answer == 0) {
                      closeOverlay();
                      beginInsert(n);
                  } else {
                      startOverOnDisc(n);
                  }
              });
    if (!canInsert) choiceScreen.setPrompt("Press", "A", "to insert and restart");
    choiceBack = [&]() { openDiscPanel(); };
}

void App::openDiscPanel() {
    std::vector<std::string> names, values;
    for (const disc::Disc& d : session.discs) {
        names.push_back(d.label);
        values.push_back(d.number == session.disc ? "In the drive" : "");
    }
    // Focus on the next disc in the drive's order, or the last.
    const int count = static_cast<int>(session.discs.size());
    int at = 0;
    for (int k = 0; k < count; ++k)
        if (session.discs[static_cast<size_t>(k)].number == session.disc) at = k;
    const int focus = at + 1 < count ? at + 1 : count - 1;
    // A row is a disc by its own number, not its place: with disc 2
    // missing on RomM the second row is disc 3.
    askChoice("Change disc", "", names, focus, [&](int k) {
        if (k >= 0 && k < static_cast<int>(session.discs.size()))
            pickDisc(session.discs[static_cast<size_t>(k)].number);
    });
    choiceScreen.setValues(values);
}

// CHANGE DISC (#162): Insert's lid closes once the game has run with
// it open; Start over starts the game again once it has gone.
void App::pumpDiscChange() {
    pumpInsert();
    if (relaunchRomId && !playing && !exitPending && !launchJob.busy()) {
        const int id = relaunchRomId;
        relaunchRomId = 0;
        if (!launchById(id)) gNextStartDisc = 0;
    }
}
