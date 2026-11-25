#include "presswriter.h"

#include <cstdio>
#include <deque>

#include "fileio.h"
#include "imagecodec.h"
#include "job.h"
#include "statefile.h"
#include "uploader.h"

namespace {

// How a state's writing ended, in the words the player is told.
struct StateDone {
    enum class How { Failed, HereOnly, Saved } how = How::Failed;
};

// One picture to make: a screenshot, or a state's.
struct Picture {
    std::shared_ptr<cab::FrameRead> frame;
    std::string path;
    bool screenshot = false;
    std::string name;
    int romId = 0;
    storage::User who;
};

// A worker and the things waiting for it. `out` is the one it has, from its
// start until its result is taken, so nothing is started over a result nobody
// has read.
template <class Thing, class Result>
struct Line {
    std::deque<Thing> waiting;
    std::shared_ptr<Thing> out;
    cab::Job<Result> job;
    size_t count() const { return waiting.size() + (out ? 1 : 0); }
};

}  // namespace

struct PressWriter::Impl {
    explicit Impl(Uploader& up) : uploader(up) {}

    Uploader& uploader;
    Line<State, StateDone> states;
    Line<Picture, bool> frames;     // PCSX2's copies
    Line<Picture, bool> pictures;
    struct Said {
        std::string text;
        MenuNotice::Tone tone;
    };
    std::vector<Said> said;

    size_t count() const { return states.count() + frames.count() + pictures.count(); }

    // To PCSX2 first where it has the frame, else to be made.
    void picture(Picture p) {
        if (p.frame->ps2Window) frames.waiting.push_back(std::move(p));
        else pictures.waiting.push_back(std::move(p));
    }

    void startState() {
        if (states.out || states.waiting.empty()) return;
        states.out = std::make_shared<State>(std::move(states.waiting.front()));
        states.waiting.pop_front();
        const std::shared_ptr<State> s = states.out;
        states.job.start([s](StateDone& done, std::string&) {
            const std::string name = s->stem + ".state";
            const std::string path = s->dir + "/" + name;
            const std::vector<uint8_t> bytes = statefile::join(s->core, s->progress);
            if (!cab::writeBytes(path, bytes)) {
                std::fprintf(stderr, "[state] could not write locally\n");
                return false;
            }
            std::fprintf(stderr, "[state] %zu bytes saved locally\n", bytes.size());
            if (s->from.stateTag.empty()) {
                std::fprintf(stderr, "[state] no settled tag for this core: it stays on this "
                                     "console\n");
                done.how = StateDone::How::HereOnly;
                return true;
            }
            // OWED AT ONCE, in the game's push (savepush.h, markState), so a
            // power cut cannot strand it.
            const std::string shot = s->frame ? s->stem + ".png" : std::string();
            if (!savepush::markState(s->from, s->who,
                                     {name, path, shot,
                                      shot.empty() ? std::string() : s->dir + "/" + shot,
                                      s->madeMs}))
                std::fprintf(stderr,
                             "[state] could not mark %s owed; it stays on this console\n",
                             name.c_str());
            done.how = StateDone::How::Saved;
            return true;
        });
    }

    void startFrame() {
        if (frames.out || frames.waiting.empty()) return;
        frames.out = std::make_shared<Picture>(std::move(frames.waiting.front()));
        frames.waiting.pop_front();
        const std::shared_ptr<Picture> p = frames.out;
        frames.job.start([p](bool& got, std::string&) {
            got = cab::fetchPs2Window(*p->frame);
            return got;
        });
    }

    void startPicture() {
        if (pictures.out || pictures.waiting.empty()) return;
        pictures.out = std::make_shared<Picture>(std::move(pictures.waiting.front()));
        pictures.waiting.pop_front();
        const std::shared_ptr<Picture> p = pictures.out;
        Uploader* const up = &uploader;
        pictures.job.start([p, up](bool& made, std::string& why) {
            std::vector<uint8_t> rgba, png;
            unsigned w = 0, h = 0;
            if (!cab::finishFrame(*p->frame, rgba, w, h) || !ui::encodePNG(rgba, w, h, png)) {
                why = "the PNG encoder refused the frame";
                return false;
            }
            if (!cab::writeBytes(p->path, png)) {
                why = "could not write " + p->path;
                return false;
            }
            made = true;
            if (!p->screenshot) {
                std::fprintf(stderr, "[state] picture %ux%u, %zu bytes\n", w, h, png.size());
                return true;
            }
            std::fprintf(stderr, "[screenshot] %ux%u, %zu bytes, %s\n", w, h, png.size(),
                         p->name.c_str());
            // On this thread: its marker is a file too, and the queue takes
            // one from anywhere.
            Uploader::Job job;
            job.romId = p->romId;
            job.emulator = "screenshot";
            job.fileName = p->name;
            job.data = std::move(png);
            job.localPath = p->path;
            job.isScreenshot = true;
            job.who = p->who;
            up->push(std::move(job));
            return true;
        });
    }

    void failed(const Picture& p, const std::string& why) {
        if (p.screenshot) {
            std::fprintf(stderr, "[screenshot] not saved: %s\n", why.c_str());
            said.push_back({"Couldn't save the screenshot", MenuNotice::Tone::Problem});
        } else {
            std::fprintf(stderr, "[state] no picture for this state: %s\n", why.c_str());
        }
    }

    // What is done taken, what waits started.
    void advance() {
        using Tone = MenuNotice::Tone;
        StateDone done;
        std::string why;
        if (states.job.take(&done, &why, nullptr)) {
            const std::shared_ptr<State> s = std::move(states.out);
            states.out.reset();
            switch (done.how) {
                case StateDone::How::Failed:
                    said.push_back({"Couldn't save the state", Tone::Problem});
                    break;
                // SAVED, and honest about the half that did not happen. A
                // state this console cannot tag is one no other device will
                // be offered, which is worth knowing before somebody relies
                // on it being there.
                case StateDone::How::HereOnly:
                    said.push_back({"Saved on this console only", Tone::Info});
                    break;
                // Nothing about RomM (decision 8 of 2026-11-15): reaching
                // RomM is taken for granted, and offline it waits on the
                // console like a save.
                case StateDone::How::Saved:
                    said.push_back({"State saved", Tone::Done});
                    break;
            }
            if (done.how != StateDone::How::Failed && s->frame)
                picture({s->frame, s->dir + "/" + s->stem + ".png", false, "", 0, s->who});
        }
        bool got = false;
        if (frames.job.take(&got, &why, nullptr)) {
            Picture p = std::move(*frames.out);
            frames.out.reset();
            if (got) pictures.waiting.push_back(std::move(p));
            else failed(p, "PCSX2 gave no frame");
        }
        bool made = false;
        if (pictures.job.take(&made, &why, nullptr)) {
            const Picture p = std::move(*pictures.out);
            pictures.out.reset();
            if (!made) failed(p, why);
            else if (p.screenshot) said.push_back({"Screenshot saved", Tone::Done});
        }
        startState();
        startFrame();
        startPicture();
    }

    // Waits for one worker that has something, the oldest kind of work first.
    void waitForOne() {
        if (states.out) states.job.wait();
        else if (frames.out) frames.job.wait();
        else if (pictures.out) pictures.job.wait();
    }

    void makeRoom() {
        while (count() >= kMostWaiting) {
            waitForOne();
            advance();
        }
    }
};

PressWriter::PressWriter(Uploader& uploader) : i_(std::make_unique<Impl>(uploader)) {}
PressWriter::~PressWriter() = default;

void PressWriter::save(State s) {
    i_->makeRoom();
    i_->states.waiting.push_back(std::move(s));
    i_->advance();
}

void PressWriter::take(Screenshot s) {
    i_->makeRoom();
    i_->picture({s.frame, s.dir + "/" + s.name, true, s.name, s.romId, s.who});
    i_->advance();
}

void PressWriter::pump(MenuNotice& notice) {
    i_->advance();
    for (Impl::Said& s : i_->said) notice.say(std::move(s.text), s.tone);
    i_->said.clear();
}

bool PressWriter::saving() const { return i_->states.count() > 0; }

void PressWriter::framesTaken() {
    while (i_->frames.count() > 0) {
        i_->advance();
        if (i_->frames.out) i_->frames.job.wait();
    }
}

void PressWriter::finish(MenuNotice& notice) {
    while (i_->count() > 0) {
        i_->advance();
        i_->waitForOne();
    }
    pump(notice);
}
