// Rewind (#78): the shortcut button and ZL, held, takes the game back up to
// about 15 seconds. docs/PROJECT.md, "Rewind is for getting back".
//
// WHAT IT IS FOR, which decided its shape. MMagTech, 2026-09-27: *"it mostly
// just [is] if you can't get a chance to save or forget and then die, you have
// the ability to rewind"*, and *"none of them have to be super smooth"*. So it
// keeps a snapshot every half second rather than every frame, and steps back
// through them one at a time while held. That is cheap enough for every system
// that has states, N64 and PlayStation included, which a frame-by-frame rewind
// is not (an N64 state is 16.8 MB).
//
// Memory only; nothing is written to the drive. The frame thread takes each
// snapshot (it must: the core runs there) and hands it over; a worker
// compresses it, so the game never waits on zlib. Stepping back decompresses on
// the frame thread, which is fine: the game is held still while it happens.

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace cab {

class Rewind {
public:
    Rewind();
    ~Rewind();

    // Everything kept goes, and a snapshot still being compressed is dropped
    // when it finishes. At a game's end, and when a state loads, since the
    // history no longer leads anywhere real.
    void reset();

    // A snapshot just taken. Returns false, taking nothing, when the worker is
    // still busy with the last one: that half second is simply skipped.
    bool offer(std::vector<uint8_t>& raw);

    // The newest snapshot, decompressed into `raw` and removed. False when
    // there is none left.
    bool takeNewest(std::vector<uint8_t>& raw);

    size_t count();
    size_t bytes();

    // How many snapshots are kept, and the most memory they may take; past
    // either, the oldest go.
    static constexpr size_t kKeep = 30;              // 15 s at one per half second
    static constexpr size_t kBudget = 192u << 20;    // a ceiling, not a target

private:
    void run();

    std::mutex m_;
    std::condition_variable wake_;
    std::thread worker_;
    bool stopping_ = false;
    bool busy_ = false;
    unsigned generation_ = 0;
    std::vector<uint8_t> pending_;
    unsigned pendingGen_ = 0;
    struct Snap {
        std::vector<uint8_t> packed;
        size_t rawSize = 0;
    };
    std::deque<Snap> snaps_;
    size_t bytes_ = 0;
};

}  // namespace cab
