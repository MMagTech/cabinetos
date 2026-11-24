// WHAT A PRESS OF SAVE STATE OR SCREENSHOT LEAVES ON THE DRIVE, WRITTEN OFF
// THE FRAME THREAD (#726). Its own file because states.cpp asks the core and
// this one only writes what it was handed, so the self-test can hand it a
// pretend game's.
//
// WHY. The frame thread runs the game and feeds its sound, and no more than a
// frame or two of sound is ever waiting (pace.cpp). Making the picture's PNG
// there stopped the game for up to a second at Picture quality's Quality, and
// writing the files for 9 to 68 ms at any level, so the sound ran out on
// nearly every press on every system (measured on the reference console,
// 2026-11-23). What stays on the frame thread is what only it can do: the
// core's own state (Core::saveState) and the frame read off the graphics chip
// (Core::readFrame).
//
// A STATE: its file written, then marked owed to RomM (savepush::markState),
// and only then said ("State saved"). ITS PICTURE AFTER THAT, under the name
// the marker already has: everything that reads the marker takes a picture
// that is not there as none (savepush.h), so a power cut in between keeps the
// state, owed, without its picture. A state that could not be written leaves
// no marker and no picture.
//
// A SCREENSHOT: its picture made and written, then queued for RomM with its
// own marker (Uploader::push), and only then said ("Screenshot saved").
//
// PLAYSTATION 2 IN ITS OWN WINDOW (#226): PCSX2's copy of its next frame is
// waited for (cab::fetchPs2Window) as the press comes, on a worker of its own,
// so a press behind another picture is still of its own moment.
//
// SEVERAL PRESSES: each has its own files. At most kMostWaiting are in hand;
// the next one waits for the oldest on the frame thread, as every press did
// before, so a press is never lost and memory is bounded (a frame can be
// 48 MB, a state 90).
//
// LEAVING THE GAME: PCSX2's copy is finished while it still runs
// (framesTaken), and everything else before the game's push is marked
// (finish; App::finishExit), so the push has the states and their pictures.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core.h"
#include "notice.h"
#include "savepush.h"
#include "users.h"

class Uploader;

class PressWriter {
public:
    static constexpr size_t kMostWaiting = 4;

    explicit PressWriter(Uploader& uploader);
    ~PressWriter();
    PressWriter(const PressWriter&) = delete;
    PressWriter& operator=(const PressWriter&) = delete;

    struct State {
        std::vector<uint8_t> core;       // the core's own bytes
        std::vector<uint8_t> progress;   // the achievements', beside them (statefile.h)
        std::string dir, stem;           // <dir>/<stem>.state and <dir>/<stem>.png
        int64_t madeMs = 0;              // milliseconds since 1970
        savepush::AimFrom from;
        storage::User who;
        std::shared_ptr<cab::FrameRead> frame;   // null for no picture
    };
    struct Screenshot {
        std::string dir, name;           // <dir>/<name>
        int romId = 0;
        storage::User who;
        std::shared_ptr<cab::FrameRead> frame;
    };

    // --- All on the frame thread ---
    void save(State s);
    void take(Screenshot s);
    // Once a frame: what is done is said, what waits is started.
    void pump(MenuNotice& notice);
    // A state asked for is not on the drive and marked yet, so Load latest
    // state waits: it would load the one before (App::loadLatestState).
    bool saving() const;
    // PCSX2's copies finished: before its window goes.
    void framesTaken();
    // Everything asked for finished and said: before the game's push is
    // marked, and before the process ends.
    void finish(MenuNotice& notice);

private:
    struct Impl;
    std::unique_ptr<Impl> i_;
};

namespace presswriter {
// `--press-writer-test` (presswriter_test.cpp). Exits.
int test();
}  // namespace presswriter
