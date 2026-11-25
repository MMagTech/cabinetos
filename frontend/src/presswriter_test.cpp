// `--press-writer-test` (#726): what a press of Save state or Screenshot leaves
// on the drive, with no core, no screen and no server. In a scratch root,
// pretend frames and states handed to the real worker, the real markers and a
// queue that sends nothing: what is on the drive when, what is said when, what
// a push made at any moment carries, and what a drive that refuses leaves. And
// how a frame read becomes a picture (cab::finishFrame). Its own file, as
// `--save-picture-test` has its own.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "cache.h"
#include "core.h"
#include "fileio.h"
#include "imagecodec.h"
#include "presswriter.h"
#include "savepush.h"
#include "storage.h"
#include "uploader.h"

namespace presswriter {
namespace {

// A frame nothing compresses, so its picture takes long enough to be caught
// not there yet (hundreds of milliseconds at 4K); `shade` in red at its top
// left corner says which press it is of.
std::shared_ptr<cab::FrameRead> noisy(unsigned w, unsigned h, uint8_t shade) {
    auto f = std::make_shared<cab::FrameRead>();
    f->px = std::make_unique_for_overwrite<uint8_t[]>(static_cast<size_t>(w) * h * 4);
    uint32_t x = 0x9E3779B9u;
    for (size_t i = 0; i < static_cast<size_t>(w) * h * 4; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        f->px[i] = static_cast<uint8_t>(x);
    }
    f->px[0] = shade;
    f->width = f->pictureWidth = w;
    f->height = f->pictureHeight = h;
    return f;
}

// A frame of one colour: its picture is made in a moment.
std::shared_ptr<cab::FrameRead> plain(uint8_t shade) {
    auto f = std::make_shared<cab::FrameRead>();
    f->px = std::make_unique_for_overwrite<uint8_t[]>(64 * 48 * 4);
    for (size_t i = 0; i < 64 * 48 * 4; ++i) f->px[i] = shade;
    f->width = f->pictureWidth = 64;
    f->height = f->pictureHeight = 48;
    return f;
}

bool exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

// The red of a picture's first pixel, -1 for no picture.
int firstRed(const std::string& path) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!exists(path) || !ui::decodeImage(cab::readBytes(path), rgba, w, h) || w <= 0) return -1;
    return rgba[0];
}

// A RomM with no channels yet that takes whatever it is sent.
struct Taker {
    std::vector<romm::SnapshotPush> pushes;
    savepush::Server server() {
        savepush::Server s;
        s.hasChannels = [] { return true; };
        s.files = [](int, bool, romm::ChannelFiles* out, std::string*) {
            *out = {7, {7}};
            return true;
        };
        s.channels = [](const std::vector<int>&, std::vector<romm::Channel>* out, std::string*,
                        bool*) {
            out->clear();
            return true;
        };
        s.push = [this](const romm::SnapshotPush& push, romm::PushAnswer* answer, std::string*) {
            pushes.push_back(push);
            answer->kind = romm::PushAnswer::Kind::Made;
            answer->status = 201;
            answer->snapshot.id = static_cast<int>(pushes.size());
            answer->snapshot.channelId = "c";
            return true;
        };
        s.saves = s.states = [](int, std::vector<romm::Asset>* out, std::string*) {
            out->clear();
            return true;
        };
        s.history = [](const std::string&, std::vector<romm::Snapshot>* out, std::string*) {
            out->clear();
            return true;
        };
        return s;
    }
};

}  // namespace

int test() {
    std::printf("what a press leaves on the drive\n\n");
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    char root[] = "/tmp/cabinetos-press-writer-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[press writer] could not make a scratch root\n");
        return 1;
    }
    setenv("CABINETOS_DRIVES", (std::string(root) + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[press writer] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    storage::User u;
    u.id = 1;
    u.name = "press-test";
    storage::setCurrentUser(u);

    Uploader up;   // never started: it queues and sends nothing
    MenuNotice notice;
    auto sessionOf = [&](int romId, const char* tag = "gambatte-native") {
        GameSession s;
        s.romId = romId;
        s.stateTag = tag;
        s.stateDir = storage::statesDir(u, "gb", romId, "gambatte");
        return s;
    };
    auto stateOf = [&](const GameSession& s, const std::string& stem, int64_t ms,
                       std::shared_ptr<cab::FrameRead> frame) {
        PressWriter::State st{std::vector<uint8_t>(4096, 0x5A), {}, s.stateDir, stem, ms, s, u,
                             std::move(frame)};
        return st;
    };
    auto shotOf = [&](int romId, const std::string& name, std::shared_ptr<cab::FrameRead> frame) {
        PressWriter::Screenshot s;
        s.dir = storage::screenshotsDir(u) + "/" + std::to_string(romId);
        s.name = name;
        s.romId = romId;
        s.who = u;
        s.frame = std::move(frame);
        return s;
    };
    // Frames drawn until no state is being saved.
    auto untilSaved = [&](PressWriter& f) {
        for (int i = 0; i < 2000 && f.saving(); ++i) {
            f.pump(notice);
            usleep(2000);
        }
    };

    std::printf("a state\n");
    {
        PressWriter f(up);
        const GameSession s = sessionOf(700);
        notice.text.clear();
        f.save(stateOf(s, "Game [a]", 1000, noisy(3840, 2160, 41)));
        check(f.saving() && notice.text.empty(),
              "as the press returns it is being saved and nothing is said");
        untilSaved(f);
        const std::string path = s.stateDir + "/Game [a].state";
        const std::string png = s.stateDir + "/Game [a].png";
        cache::OwedPush p;
        check(cab::readBytes(path).size() == 4096 && notice.text == "State saved",
              "\"State saved\" once it is on the drive");
        check(cache::owedPush(u, 700, &p) && p.states.size() == 1 && p.states[0].path == path &&
                  p.states[0].pictureName == "Game [a].png" && p.states[0].picturePath == png,
              "and owed to RomM by then, with the name its picture will have");
        check(!exists(png), "its picture is not made yet: the game never waited for it");
        const auto offered = savepush::offeredStates(u, 700, s.stateTag, s.stateDir, nullptr);
        check(offered.size() == 1 && offered[0].path == path && offered[0].picture.empty(),
              "Load latest state has it by then, with no picture");
        // A POWER CUT HERE: what the next start's push carries.
        Taker cut;
        std::string why;
        check(savepush::send(cut.server(), u, 700, &why) == savepush::Sent::Landed &&
                  cut.pushes.size() == 1 && cut.pushes[0].states.size() == 1 &&
                  cut.pushes[0].states[0].file.bytes.size() == 4096 &&
                  cut.pushes[0].states[0].file.picture.empty(),
              "a power cut now keeps the state, sent without its picture");
        f.finish(notice);
        check(firstRed(png) == 41, "the picture of that moment, made after");
    }
    {
        // The game left right after the press: the push waits for both.
        PressWriter f(up);
        const GameSession s = sessionOf(701);
        f.save(stateOf(s, "Game [b]", 1000, noisy(1280, 720, 42)));
        f.framesTaken();
        f.finish(notice);
        Taker leave;
        std::string why;
        check(savepush::send(leave.server(), u, 701, &why) == savepush::Sent::Landed &&
                  leave.pushes.size() == 1 && leave.pushes[0].states.size() == 1 &&
                  !leave.pushes[0].states[0].file.picture.empty(),
              "the game left right after the press: the state goes with its picture");
    }
    {
        PressWriter f(up);
        const GameSession s = sessionOf(702);
        f.save(stateOf(s, "Game [c]", 1000, plain(1)));
        f.save(stateOf(s, "Game [d]", 2000, plain(2)));
        f.finish(notice);
        cache::OwedPush p;
        check(cache::owedPush(u, 702, &p) && p.states.size() == 2 &&
                  p.states[0].fileName == "Game [d].state" &&
                  firstRed(s.stateDir + "/Game [c].png") == 1 &&
                  firstRed(s.stateDir + "/Game [d].png") == 2,
              "a second press before the first is done: both, each with its own picture");
    }
    {
        PressWriter f(up);
        const GameSession s = sessionOf(703, "");
        f.save(stateOf(s, "Game [e]", 1000, plain(3)));
        f.finish(notice);
        check(notice.text == "Saved on this console only" && !cache::isPushOwed(u, 703) &&
                  exists(s.stateDir + "/Game [e].state") &&
                  firstRed(s.stateDir + "/Game [e].png") == 3,
              "a core whose states never travel: \"Saved on this console only\", nothing owed");
    }
    {
        PressWriter f(up);
        const GameSession s = sessionOf(704);
        f.save(stateOf(s, "Game [f]", 1000, nullptr));
        f.finish(notice);
        cache::OwedPush p;
        check(cache::owedPush(u, 704, &p) && p.states.size() == 1 &&
                  p.states[0].picturePath.empty() && notice.text == "State saved",
              "a state with no frame to picture: saved and owed without one");
    }
    {
        // ITS FOLDER IS A FILE, so nothing can be written in it.
        PressWriter f(up);
        GameSession s = sessionOf(705);
        s.stateDir = std::string(root) + "/not-a-folder";
        cab::writeBytes(s.stateDir, std::string("a file"));
        f.save(stateOf(s, "Game [g]", 1000, plain(4)));
        f.finish(notice);
        check(notice.text == "Couldn't save the state" && !cache::isPushOwed(u, 705),
              "a drive that refuses the state: said, nothing owed");
    }
    {
        // The state is written; the file its picture is written as (writeBytes'
        // temporary) is a folder that is not empty, so it cannot be.
        PressWriter f(up);
        const GameSession s = sessionOf(706);
        storage::makeDirs(s.stateDir + "/Game [h].png.placing/in the way");
        f.save(stateOf(s, "Game [h]", 1000, plain(5)));
        f.finish(notice);
        cache::OwedPush p;
        Taker t;
        std::string why;
        check(notice.text == "State saved" && cache::owedPush(u, 706, &p) &&
                  savepush::send(t.server(), u, 706, &why) == savepush::Sent::Landed &&
                  t.pushes[0].states.size() == 1 && t.pushes[0].states[0].file.picture.empty(),
              "a picture that cannot be written: the state stays saved, and goes without it");
    }

    std::printf("\na screenshot\n");
    {
        PressWriter f(up);
        const int before = up.pending();
        notice.text.clear();
        f.take(shotOf(710, "Shot [a].png", noisy(3840, 2160, 51)));
        const std::string path = storage::screenshotsDir(u) + "/710/Shot [a].png";
        f.pump(notice);
        check(!exists(path) && notice.text.empty() && up.pending() == before,
              "as the press returns: not made yet, nothing said, nothing queued");
        f.finish(notice);
        bool owed = false;
        for (const cache::Owed& o : cache::owed(u))
            owed = owed || (o.isScreenshot && o.romId == 710 && o.localPath == path);
        check(firstRed(path) == 51 && notice.text == "Screenshot saved" && owed &&
                  up.pending() == before + 1,
              "made, marked owed and queued for RomM, and only then said");
    }
    {
        PressWriter f(up);
        const int before = up.pending();
        PressWriter::Screenshot s = shotOf(711, "Shot [b].png", plain(6));
        s.dir = std::string(root) + "/not-a-folder";
        f.take(std::move(s));
        f.finish(notice);
        check(notice.text == "Couldn't save the screenshot" && up.pending() == before,
              "a drive that refuses it: said, nothing queued");
    }
    {
        PressWriter f(up);
        auto frame = std::make_shared<cab::FrameRead>();
        frame->ps2Window = true;
        f.take(shotOf(712, "Shot [c].png", frame));
        f.framesTaken();
        notice.text.clear();
        f.finish(notice);
        check(notice.text == "Couldn't save the screenshot",
              "PlayStation 2's window gives no frame: \"Couldn't save the screenshot\"");
    }
    {
        // SEVEN PRESSES BEFORE ONE FRAME IS DRAWN.
        PressWriter f(up);
        auto name = [](int i) { return "Shot [" + std::to_string(i) + "].png"; };
        const std::string dir = storage::screenshotsDir(u) + "/713/";
        for (int i = 0; i < 7; ++i)
            f.take(shotOf(713, name(i), noisy(1920, 1080, static_cast<uint8_t>(60 + i))));
        int there = 0;
        for (int i = 0; i < 7; ++i) there += exists(dir + name(i)) ? 1 : 0;
        check(there >= 7 - static_cast<int>(PressWriter::kMostWaiting) && there < 7,
              "seven presses at once: at most four wait, the rest are done by then");
        f.finish(notice);
        bool all = true;
        for (int i = 0; i < 7; ++i) all = all && firstRed(dir + name(i)) == 60 + i;
        check(all, "and every press has its own picture");
    }
    {
        // THE PERSON IT WAS MADE AS, whoever is signed in when it is written.
        PressWriter f(up);
        storage::User other = u;
        other.id = 2;
        other.name = "second";
        f.take(shotOf(714, "Shot [d].png", noisy(1920, 1080, 70)));
        f.save(stateOf(sessionOf(714), "Game [i]", 1000, plain(7)));
        storage::setCurrentUser(other);
        f.finish(notice);
        cache::OwedPush p;
        bool mine = false, theirs = false;
        for (const cache::Owed& o : cache::owed(u)) mine = mine || o.romId == 714;
        for (const cache::Owed& o : cache::owed(other)) theirs = theirs || o.romId == 714;
        check(mine && !theirs && cache::isPushOwed(u, 714) && !cache::isPushOwed(other, 714),
              "owed by the person who pressed, not whoever is signed in after");
        storage::setCurrentUser(u);
    }

    std::printf("\na state marked while another mark is made\n");
    {
        bool both = true;
        for (int round = 0; round < 60; ++round) {
            const GameSession s = sessionOf(800 + round);
            auto made = [&](const char* name, int64_t ms) {
                savepush::StateMade m;
                m.fileName = name;
                m.path = s.stateDir + "/" + name;
                m.madeMs = ms;
                return m;
            };
            std::thread other([&] { savepush::markState(s, u, made("other.state", 1)); });
            savepush::markState(s, u, made("this.state", 2));
            other.join();
            cache::OwedPush p;
            both = both && cache::owedPush(u, s.romId, &p) && p.states.size() == 2;
        }
        check(both, "neither is lost, sixty times over");
    }

    std::printf("\na frame read made a picture\n");
    {
        // Three across and two down, each pixel its own red: 10 to 15.
        auto six = [](unsigned turns, bool bottomUp) {
            cab::FrameRead f;
            f.px = std::make_unique<uint8_t[]>(6 * 4);
            for (int i = 0; i < 6; ++i) f.px[static_cast<size_t>(i) * 4] = static_cast<uint8_t>(10 + i);
            f.width = f.pictureWidth = 3;
            f.height = f.pictureHeight = 2;
            f.quarterTurns = turns;
            f.bottomUp = bottomUp;
            return f;
        };
        auto reds = [](cab::FrameRead f, unsigned* w, unsigned* h, bool* opaque) {
            std::vector<uint8_t> rgba;
            std::vector<int> out;
            *opaque = true;
            if (!cab::finishFrame(f, rgba, *w, *h)) return out;
            for (size_t i = 0; i < rgba.size(); i += 4) {
                out.push_back(rgba[i]);
                *opaque = *opaque && rgba[i + 3] == 0xFF;
            }
            return out;
        };
        unsigned w = 0, h = 0;
        bool opaque = false;
        check(reds(six(0, false), &w, &h, &opaque) == std::vector<int>({10, 11, 12, 13, 14, 15}) &&
                  w == 3 && h == 2 && opaque,
              "a frame read top row first: as it is, opaque");
        check(reds(six(0, true), &w, &h, &opaque) == std::vector<int>({13, 14, 15, 10, 11, 12}),
              "read bottom row first: turned the right way up");
        check(reds(six(1, false), &w, &h, &opaque) == std::vector<int>({12, 15, 11, 14, 10, 13}) &&
                  w == 2 && h == 3,
              "a quarter turn anticlockwise: upright, two across and three down");
        check(reds(six(2, false), &w, &h, &opaque) == std::vector<int>({15, 14, 13, 12, 11, 10}) &&
                  w == 3 && h == 2,
              "a half turn");
        check(reds(six(3, false), &w, &h, &opaque) == std::vector<int>({13, 10, 14, 11, 15, 12}) &&
                  w == 2 && h == 3,
              "a quarter turn clockwise");
        check(reds(six(1, true), &w, &h, &opaque) == std::vector<int>({15, 12, 14, 11, 13, 10}),
              "read bottom row first and turned: both");
        cab::FrameRead wide = six(0, false);
        wide.pictureWidth = 6;
        check(reds(std::move(wide), &w, &h, &opaque) ==
                      std::vector<int>({10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15}) &&
                  w == 6 && h == 2,
              "stretched to the game's shape");
        cab::FrameRead none;
        std::vector<uint8_t> rgba;
        check(!cab::finishFrame(none, rgba, w, h), "a frame with nothing read makes no picture");
        // The PNG, made in one pass: what went in comes out.
        std::vector<uint8_t> in(37 * 23 * 4), png, out;
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = i % 4 == 3 ? 0xFF : static_cast<uint8_t>(i * 7 + i / 11);
        int pw = 0, ph = 0;
        check(ui::encodePNG(in, 37, 23, png) && ui::decodeImage(png, out, pw, ph) && pw == 37 &&
                  ph == 23 && out == in && png.size() > 8 &&
                  std::string(png.end() - 8, png.end()).substr(0, 4) == "IEND",
              "the PNG holds the picture exactly, and ends where its file ends");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

}  // namespace presswriter
