#include "guncamera.h"

#include "fileio.h"
#include "storage.h"

#include <json-c/json.h>
#include <linux/videodev2.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace guncamera {
namespace {

// Where the cameras and the TV are looked for; the self-test points them at
// a tree of its own.
std::string gSysVideo = "/sys/class/video4linux";
std::string gDevDir = "/dev";
std::string gSysDrm = "/sys/class/drm";

std::string readText(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string bytesOf(const std::string& path) {
    const std::vector<uint8_t> b = cab::readBytes(path);
    return std::string(b.begin(), b.end());
}

std::string parentOf(const std::string& p) {
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? std::string() : p.substr(0, slash);
}

std::string realOf(const std::string& path) {
    char real[PATH_MAX];
    return ::realpath(path.c_str(), real) ? std::string(real) : std::string();
}

// --- The memory, per TV and gun ----------------------------------------------

std::mutex gMemory;

std::string memoryPath() { return storage::configDir() + "/lightgun-cameras.json"; }

json_object* loadMemory() {
    const std::string body = bytesOf(memoryPath());
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_object) return o;
    if (o) json_object_put(o);
    return json_object_new_object();
}

bool onLadder(int v) { return v >= kLadder[0] && v <= kLadder[kRungs - 1]; }

// --- A camera ------------------------------------------------------------------------

struct Cam {
    std::string node;
    int fd = -1;
    int min = 0, max = 0;
};

void closeCam(Cam& c) {
    if (c.fd >= 0) ::close(c.fd);
    c = Cam{};
}

bool openCam(Cam& c, const std::string& node) {
    const int fd = ::open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    v4l2_queryctrl q{};
    q.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    if (::ioctl(fd, VIDIOC_QUERYCTRL, &q) != 0 || (q.flags & V4L2_CTRL_FLAG_DISABLED) ||
        q.maximum <= q.minimum) {
        ::close(fd);
        return false;
    }
    c.node = node;
    c.fd = fd;
    c.min = q.minimum;
    c.max = q.maximum;
    return true;
}

// -1 when the camera does not answer.
int getExposure(const Cam& c) {
    v4l2_control k{};
    k.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    return ::ioctl(c.fd, VIDIOC_G_CTRL, &k) == 0 ? k.value : -1;
}

bool setExposure(const Cam& c, int v) {
    v4l2_control k{};
    k.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    k.value = std::clamp(v, c.min, c.max);
    return ::ioctl(c.fd, VIDIOC_S_CTRL, &k) == 0;
}

// --- The worker ------------------------------------------------------------------------

enum class Ask { None, Calibrate, Cancel };

struct PerGun {
    std::string usb;          // the gun this is for; empty while none
    std::string key;          // its memory's key (gunKey)
    Cam cam;
    int64_t nextOpen = 0;     // when its camera is looked for again
    bool saidNoCamera = false;
    int want = -1;            // the exposure kept, -1 for Sinden's own
    int64_t nextCheck = 0;
    Sweep sweep;
    bool sweeping = false, forCalibration = false;
    int before = -1;          // the exposure the sweep began from
    int given = -1;           // the rung it last gave the camera
    int64_t sweepEnded = INT64_MIN / 2;
    int64_t nextTroubleLook = 0;
};

std::mutex gLock;   // gAsk, gShown
PerGun gGuns[lightgun::kMaxGuns];
Ask gAsk[lightgun::kMaxGuns] = {};
Progress gShown[lightgun::kMaxGuns];
std::atomic<bool> gInPlay{false}, gCalibrating{false};
std::once_flag gStarted;

Ask takeAsk(int n) {
    std::lock_guard<std::mutex> lk(gLock);
    const Ask a = gAsk[n];
    gAsk[n] = Ask::None;
    return a;
}

void show(int n, const Progress& p) {
    std::lock_guard<std::mutex> lk(gLock);
    gShown[n] = p;
}

void give(int n, PerGun& g, int v, const char* why) {
    if (setExposure(g.cam, v)) {
        if (why) std::fprintf(stderr, "[guncamera] gun %d's camera set to %d: %s\n", n + 1, v, why);
    } else {
        std::fprintf(stderr, "[guncamera] gun %d's camera (%s) refused exposure %d: %s\n", n + 1,
                     g.cam.node.c_str(), v, std::strerror(errno));
    }
}

void startSweep(int n, PerGun& g, int64_t t, bool forCalibration) {
    g.before = getExposure(g.cam);
    g.given = -1;
    g.sweep.begin(t);
    g.sweeping = true;
    g.forCalibration = forCalibration;
    if (forCalibration) show(n, Progress{true, false, 0.0f, -1, false});
}

// A sweep stopped before its end: the camera back where it was.
void stopSweep(int n, PerGun& g) {
    if (!g.sweeping) return;
    g.sweeping = false;
    if (g.before >= 0 && g.cam.fd >= 0) give(n, g, g.before, "a sweep stopped, put back");
    if (g.forCalibration) show(n, Progress{});
}

void finishSweep(int n, PerGun& g, int64_t t) {
    g.sweeping = false;
    g.sweepEnded = t;
    const int r = g.sweep.result();
    char line[256];
    int at = 0;
    for (int i = 0; i < kRungs && at < static_cast<int>(sizeof line) - 16; ++i)
        at += std::snprintf(line + at, sizeof line - static_cast<size_t>(at), "%s%d %.2f",
                            i ? ", " : "", kLadder[i], static_cast<double>(g.sweep.seen[i]));
    std::fprintf(stderr, "[guncamera] gun %d swept%s: %s\n", n + 1,
                 g.forCalibration ? " for calibration" : " in the game", line);
    if (r >= 0) {
        g.want = r;
        remember(tvNow(), g.key, r);
        give(n, g, r, "the middle of what it saw");
    } else if (g.before >= 0) {
        give(n, g, g.before, "it saw the border at no exposure, put back");
    }
    if (g.forCalibration) show(n, Progress{false, true, 1.0f, r, false});
}

void tickGun(int n, int64_t t, bool follow) {
    PerGun& g = gGuns[n];
    const Ask ask = takeAsk(n);
    const lightgun::Where w = lightgun::where(n);
    if (w.usb.empty()) {
        if (!g.usb.empty()) {
            closeCam(g.cam);
            g = PerGun{};
        }
        if (ask == Ask::Calibrate) show(n, Progress{false, true, 0.0f, -1, true});
        return;
    }
    if (g.usb != w.usb) {
        closeCam(g.cam);
        g = PerGun{};
        g.usb = w.usb;
        g.key = gunKey(w);
        const std::string tv = tvNow();
        g.want = remembered(tv, g.key);
        if (g.want >= 0)
            std::fprintf(stderr, "[guncamera] gun %d (%s) keeps exposure %d on TV %s\n", n + 1,
                         g.key.c_str(), g.want, tv.c_str());
        else
            std::fprintf(stderr,
                         "[guncamera] gun %d (%s): nothing remembered on TV %s; Sinden's own "
                         "until a sweep\n",
                         n + 1, g.key.c_str(), tv.c_str());
    }
    if (g.cam.fd < 0 && t >= g.nextOpen) {
        const std::string node = cameraOf(w.usb);
        if (!node.empty() && openCam(g.cam, node)) {
            std::fprintf(stderr, "[guncamera] gun %d's camera is %s (exposure %d to %d)\n", n + 1,
                         node.c_str(), g.cam.min, g.cam.max);
            g.saidNoCamera = false;
            g.nextCheck = t;
        } else {
            if (!g.saidNoCamera)
                std::fprintf(stderr, "[guncamera] gun %d: no camera the console can reach%s%s\n",
                             n + 1, node.empty() ? "" : " at ", node.c_str());
            g.saidNoCamera = true;
            g.nextOpen = t + 1000;
        }
    }
    if (g.cam.fd < 0) {
        if (ask == Ask::Calibrate) show(n, Progress{false, true, 0.0f, -1, true});
        return;
    }
    if (ask == Ask::Cancel) stopSweep(n, g);
    if (ask == Ask::Calibrate) startSweep(n, g, t, true);

    if (g.sweeping) {
        if (g.given != g.sweep.exposure()) {
            g.given = g.sweep.exposure();
            give(n, g, g.given, nullptr);
        }
        if (g.sweep.tick(t, lightgun::sight(n, g.sweep.lookFrom()))) finishSweep(n, g, t);
        else if (g.forCalibration) show(n, Progress{true, false, g.sweep.progress(), -1, false});
        return;
    }
    // KEPT: the camera is set again when it is not at the exposure kept, as
    // after Sinden's software starts again, which sets its own.
    if (t >= g.nextCheck) {
        g.nextCheck = t + 1000;
        if (g.want >= 0) {
            const int now = getExposure(g.cam);
            if (now < 0) {
                std::fprintf(stderr, "[guncamera] gun %d's camera %s does not answer: %s\n",
                             n + 1, g.cam.node.c_str(), std::strerror(errno));
                closeCam(g.cam);
                g.nextOpen = t + 1000;
                return;
            }
            if (now != g.want) {
                char why[64];
                std::snprintf(why, sizeof why, "it was at %d", now);
                give(n, g, g.want, why);
            }
        }
    }
    // FOLLOWED in a game: trouble starts a sweep, at most every kSweepAgainMs.
    if (follow && t >= g.nextTroubleLook) {
        g.nextTroubleLook = t + 250;
        const Trouble tr = trouble(lightgun::sight(n, t - kTroubleMs));
        if (tr != Trouble::None && t - g.sweepEnded >= kSweepAgainMs) {
            std::fprintf(stderr, "[guncamera] gun %d %s at exposure %d: sweeping\n", n + 1,
                         tr == Trouble::Flicker ? "keeps losing the border"
                                                : "shoots with the border unseen",
                         getExposure(g.cam));
            startSweep(n, g, t, false);
        }
    }
}

void run() {
    int64_t playSince = -1;
    for (;;) {
        const bool inPlay = gInPlay, calibrating = gCalibrating;
        const int64_t t = lightgun::clockMs();
        if (!inPlay && !calibrating) {
            // Nothing wanted: a sweep under way stops, its camera put back,
            // and no camera is held open.
            for (int n = 0; n < lightgun::kMaxGuns; ++n) {
                stopSweep(n, gGuns[n]);
                closeCam(gGuns[n].cam);
                gGuns[n].nextOpen = 0;
            }
            playSince = -1;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        if (!inPlay) playSince = -1;
        else if (playSince < 0) playSince = t;
        const bool follow = inPlay && !calibrating && t - playSince >= kFollowAfterMs;
        for (int n = 0; n < lightgun::kMaxGuns; ++n) {
            // In a game, paused or not, only a calibration's sweep: one of
            // the game's stops as the game is paused.
            if (!inPlay && gGuns[n].sweeping && !gGuns[n].forCalibration) stopSweep(n, gGuns[n]);
            tickGun(n, t, follow);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}  // namespace

// --- Public ------------------------------------------------------------------------

int middleOf(const float (&seen)[kRungs]) {
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < kRungs;) {
        if (seen[i] < kSees) {
            ++i;
            continue;
        }
        int j = i;
        while (j < kRungs && seen[j] >= kSees) ++j;
        if (j - i > bestLen) {
            bestLen = j - i;
            bestStart = i;
        }
        i = j;
    }
    return bestStart < 0 ? -1 : kLadder[bestStart + (bestLen - 1) / 2];
}

void Sweep::begin(int64_t t) {
    rung_ = 0;
    at_ = t;
    std::fill(std::begin(seen), std::end(seen), 0.0f);
}

bool Sweep::tick(int64_t t, const lightgun::Sight& s) {
    if (!running() || t < lookFrom() + kLookMs) return false;
    const int64_t total = s.onMs + s.offMs;
    seen[rung_] = total > 0 ? static_cast<float>(s.onMs) / static_cast<float>(total) : 0.0f;
    ++rung_;
    at_ = t;
    return rung_ == kRungs;
}

float Sweep::progress() const {
    return rung_ < 0 ? 0.0f : static_cast<float>(std::min(rung_, kRungs)) / kRungs;
}

Trouble trouble(const lightgun::Sight& s) {
    if (!s.present) return Trouble::None;
    if (s.flips >= 6) return Trouble::Flicker;
    if (s.shots >= 3 && s.onMs == 0) return Trouble::Blind;
    return Trouble::None;
}

std::string tvKey(const std::string& e) {
    static const unsigned char kHead[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    if (e.size() < 128 || std::memcmp(e.data(), kHead, sizeof kHead) != 0) return {};
    auto b = [&](size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(e[i])); };
    const unsigned maker = (b(8) << 8) | b(9);
    const char letters[4] = {static_cast<char>('@' + ((maker >> 10) & 31)),
                             static_cast<char>('@' + ((maker >> 5) & 31)),
                             static_cast<char>('@' + (maker & 31)), 0};
    const unsigned product = b(10) | (b(11) << 8);
    const unsigned serial = b(12) | (b(13) << 8) | (b(14) << 16) | (b(15) << 24);
    char out[32];
    std::snprintf(out, sizeof out, "%s-%04x-%08x", letters, product, serial);
    return out;
}

std::string tvNow() {
    // The TV answering now: the kernel's copy of the EDID of the first
    // connector that says it is connected and holds one.
    std::vector<std::string> names;
    if (DIR* d = ::opendir(gSysDrm.c_str())) {
        while (dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name.rfind("card", 0) == 0 && name.find('-') != std::string::npos)
                names.push_back(name);
        }
        ::closedir(d);
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        const std::string at = gSysDrm + "/" + name;
        if (readText(at + "/status") != "connected") continue;
        const std::string key = tvKey(bytesOf(at + "/edid"));
        if (!key.empty()) return key;
    }
    // None answers: gamescope's copy of the last TV's (cabinetos-session).
    if (const char* home = std::getenv("HOME")) {
        const std::string key =
            tvKey(bytesOf(std::string(home) + "/.local/state/cabinetos/gamescope/edid.bin"));
        if (!key.empty()) return key;
    }
    return "unknown";
}

std::string gunKey(const lightgun::Where& w) { return w.id + "@" + w.port; }

int remembered(const std::string& tv, const std::string& gun) {
    std::lock_guard<std::mutex> lk(gMemory);
    json_object* o = loadMemory();
    int out = -1;
    json_object* forTv = nullptr;
    if (json_object_object_get_ex(o, tv.c_str(), &forTv) &&
        json_object_get_type(forTv) == json_type_object) {
        json_object* v = nullptr;
        if (json_object_object_get_ex(forTv, gun.c_str(), &v) &&
            json_object_get_type(v) == json_type_int) {
            out = json_object_get_int(v);
        } else {
            // Another gun's, the newest: the last one written.
            json_object_object_foreach(forTv, k, val) {
                (void)k;
                if (json_object_get_type(val) == json_type_int) out = json_object_get_int(val);
            }
        }
    }
    json_object_put(o);
    return onLadder(out) ? out : -1;
}

void remember(const std::string& tv, const std::string& gun, int exposure) {
    std::lock_guard<std::mutex> lk(gMemory);
    json_object* o = loadMemory();
    json_object* forTv = nullptr;
    if (!json_object_object_get_ex(o, tv.c_str(), &forTv) ||
        json_object_get_type(forTv) != json_type_object) {
        forTv = json_object_new_object();
        json_object_object_add(o, tv.c_str(), forTv);
    }
    // Out, then in again: the newest is the last.
    json_object_object_del(forTv, gun.c_str());
    json_object_object_add(forTv, gun.c_str(), json_object_new_int(exposure));
    const std::string text =
        std::string(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY)) + "\n";
    json_object_put(o);
    if (!cab::writeBytes(memoryPath(), text))
        std::fprintf(stderr, "[guncamera] could not write %s\n", memoryPath().c_str());
}

std::string cameraOf(const std::string& gunUsb) {
    const std::string hub = parentOf(gunUsb);
    if (hub.empty()) return {};
    std::string found;
    DIR* d = ::opendir(gSysVideo.c_str());
    if (!d) return {};
    bool two = false;
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.rfind("video", 0) != 0) continue;
        // Its device is the camera's USB interface; above it the camera, and
        // above that the hub it hangs from.
        const std::string camera = parentOf(realOf(gSysVideo + "/" + name + "/device"));
        if (camera.empty() || parentOf(camera) != hub) continue;
        if (readText(camera + "/product").rfind("SindenCam", 0) != 0) continue;
        if (readText(gSysVideo + "/" + name + "/index") != "0") continue;
        two = two || !found.empty();
        found = gDevDir + "/" + name;
    }
    ::closedir(d);
    return two ? std::string() : found;
}

void start() {
    std::call_once(gStarted, [] { std::thread(run).detach(); });
}

void setWanted(bool inPlay, bool calibrating) {
    gInPlay = inPlay;
    gCalibrating = calibrating;
}

void calibrate(int n) {
    if (n < 0 || n >= lightgun::kMaxGuns) return;
    std::lock_guard<std::mutex> lk(gLock);
    gAsk[n] = Ask::Calibrate;
    gShown[n] = Progress{true, false, 0.0f, -1, false};
}

void cancel(int n) {
    if (n < 0 || n >= lightgun::kMaxGuns) return;
    std::lock_guard<std::mutex> lk(gLock);
    gAsk[n] = Ask::Cancel;
    gShown[n] = Progress{};
}

Progress progress(int n) {
    if (n < 0 || n >= lightgun::kMaxGuns) return {};
    std::lock_guard<std::mutex> lk(gLock);
    return gShown[n];
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // ---- The ladder
    {
        bool rising = true;
        for (int i = 1; i < kRungs; ++i) rising = rising && kLadder[i] > kLadder[i - 1];
        bool even = true;
        for (int i = 1; i < kRungs; ++i) {
            const float step = static_cast<float>(kLadder[i]) / static_cast<float>(kLadder[i - 1]);
            even = even && step > 1.2f && step < 1.34f;
        }
        check(rising && even && kLadder[0] == 15 && kLadder[kRungs - 1] == 160,
              "the ladder runs from 15 to 160 (1.5 ms to 16 ms), each step about a quarter more");
    }

    // ---- The middle, against the exposures measured on the reference
    // console. A rung the measurements did not cover is given its worse
    // measured neighbour's share, so nothing here rests on a guess in the
    // ladder's favour.
    {
        // 2026-11-27, night: nothing at 10, the border seen at every exposure
        // from 15 to 160.
        const float night[kRungs] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        // 2026-11-28, 08:35, daylight: seen from 10 to 160.
        const float morning[kRungs] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        // 2026-11-28, 09:50, brighter: 78 lost about three readings in four,
        // 40 seen 510 of 510, 20 "mostly". 15 is under 20, 24 and 30 between
        // 20 and 40, 49 and 62 between 40 and 78, and 100 to 160 over 78, so
        // each takes its worse measured neighbour's share.
        const float bright[kRungs] = {0.8f, 0.8f, 0.8f, 0.8f, 1.0f, 0.25f,
                                      0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
        check(middleOf(night) == 49 && middleOf(morning) == 49,
              "at night and in the morning's daylight the border is seen all the way up, and the "
              "middle is 49 (4.9 ms), inside both measured ranges");
        check(middleOf(bright) == 39,
              "in the brighter room it is 39, the rung of the 40 measured to see the border 510 "
              "readings of 510, never Sinden's 78");
        const float none[kRungs] = {};
        const float split[kRungs] = {1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0};
        const float even[kRungs] = {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0};
        const float edge[kRungs] = {0.89f, 0.9f, 0.95f, 0, 0, 0, 0, 0, 0, 0, 0};
        check(middleOf(none) == -1 && middleOf(split) == 39 && middleOf(even) == 30 &&
                  middleOf(edge) == 19,
              "no rung seeing it is none; of two runs the longer; of two middles the lower; and "
              "nine tenths of a look is seeing it, less is not");
    }

    // ---- A sweep, driven as the worker drives it, on a gun whose camera
    // sees the border at each rung for that rung's share of the time
    {
        auto drive = [&](const float (&model)[kRungs], std::vector<int>* visited,
                         int64_t* took) {
            Sweep s;
            s.begin(1000);
            int64_t t = 1000;
            for (;;) {
                const int e = s.exposure();
                if (visited->empty() || visited->back() != e) visited->push_back(e);
                int rung = 0;
                while (kLadder[rung] != e) ++rung;
                lightgun::Sight seen;
                seen.present = true;
                const int64_t looked = std::max<int64_t>(0, t - s.lookFrom());
                seen.onMs = static_cast<int64_t>(model[rung] * static_cast<float>(looked));
                seen.offMs = looked - seen.onMs;
                if (s.tick(t, seen)) break;
                t += 20;
                if (t > 60000) break;
            }
            *took = t - 1000;
            return s.result();
        };
        const float bright[kRungs] = {0.8f, 0.8f, 0.8f, 0.8f, 1.0f, 0.25f,
                                      0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
        std::vector<int> visited;
        int64_t took = 0;
        const int r = drive(bright, &visited, &took);
        check(r == 39 && visited == std::vector<int>(std::begin(kLadder), std::end(kLadder)),
              "a sweep gives the camera each rung in turn and lands on the middle it measured");
        check(took >= kRungs * (Sweep::kSettleMs + Sweep::kLookMs) && took <= 4500,
              "and takes about four seconds: each rung settled, then looked at");
        Sweep fresh;
        lightgun::Sight early;
        early.present = true;
        early.onMs = 100;
        fresh.begin(0);
        check(!fresh.tick(Sweep::kSettleMs + Sweep::kLookMs - 1, early) && fresh.progress() == 0.0f,
              "a rung is not judged before its look is over");
    }

    // ---- Trouble in a game
    {
        lightgun::Sight at78;   // 132 readings in 2 s, one in four on the screen
        at78.present = true;
        at78.onMs = 500;
        at78.offMs = 1500;
        at78.flips = 33;
        lightgun::Sight at40;   // 510 of 510 on the screen
        at40.present = true;
        at40.onMs = 2000;
        lightgun::Sight reload;   // aimed off the screen once for a reload, and a shot there
        reload.present = true;
        reload.onMs = 1700;
        reload.offMs = 300;
        reload.flips = 1;
        reload.shots = 2;
        reload.shotsOff = 1;
        lightgun::Sight down;   // put down on the couch
        down.present = true;
        down.offMs = 2000;
        lightgun::Sight blind;   // shooting at a screen the camera cannot see
        blind.present = true;
        blind.offMs = 2000;
        blind.shots = 3;
        blind.shotsOff = 3;
        lightgun::Sight gone;
        check(trouble(at78) == Trouble::Flicker,
              "losing the border three readings in four, as at 78 in the bright room, is trouble");
        check(trouble(blind) == Trouble::Blind,
              "and so is the trigger pulled three times with the border never seen");
        check(trouble(at40) == Trouble::None && trouble(reload) == Trouble::None &&
                  trouble(down) == Trouble::None && trouble(gone) == Trouble::None,
              "seeing it all the while, a reload off the screen, a gun put down and no gun are "
              "not");
    }

    // ---- The TV, by its EDID
    {
        std::string edid(256, '\0');
        const unsigned char head[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
                                      0x1e, 0x6d,              // "GSM"
                                      0xa4, 0xc0,              // product 0xc0a4
                                      0x01, 0x01, 0x01, 0x01}; // serial
        std::memcpy(edid.data(), head, sizeof head);
        check(tvKey(edid) == "GSM-c0a4-01010101",
              "a TV is its EDID's maker, product code and serial number");
        check(tvKey(edid.substr(0, 100)).empty() && tvKey(std::string(128, 'x')).empty(),
              "and a file that is no EDID is no TV");

        char tmpl[] = "/tmp/cabinetos-guncam-XXXXXX";
        const char* made = ::mkdtemp(tmpl);
        check(made != nullptr, "a scratch folder");
        if (made) {
            const std::string root = made;
            std::error_code ec;
            const std::string drm = root + "/drm";
            cab::writeBytes(drm + "/card1-DP-1/status", std::string("disconnected\n"));
            cab::writeBytes(drm + "/card1-HDMI-A-1/status", std::string("connected\n"));
            cab::writeBytes(drm + "/card1-HDMI-A-1/edid", edid);
            const std::string savedHome = std::getenv("HOME") ? std::getenv("HOME") : "";
            ::setenv("HOME", (root + "/home").c_str(), 1);
            gSysDrm = drm;
            const std::string connected = tvNow();
            cab::writeBytes(drm + "/card1-HDMI-A-1/status", std::string("disconnected\n"));
            const std::string none = tvNow();
            std::string other = edid;
            other[15] = 0x02;
            cab::writeBytes(root + "/home/.local/state/cabinetos/gamescope/edid.bin", other);
            const std::string last = tvNow();
            check(connected == "GSM-c0a4-01010101" && none == "unknown" &&
                      last == "GSM-c0a4-02010101",
                  "the TV is the connected screen's, else gamescope's copy of the last TV's, else "
                  "unknown");
            gSysDrm = "/sys/class/drm";
            ::setenv("HOME", savedHome.c_str(), 1);

            // ---- Remembered per gun and TV, in a storage root of the test's
            storage::setRoot(root + "/root");
            const std::string gunA = "16c0:0f01@0000:c8:00.0-1.2.2";
            const std::string gunB = "16c0:0f02@0000:c8:00.0-1.3.2";
            const bool empty = remembered("GSM-c0a4-01010101", gunA) == -1;
            remember("GSM-c0a4-01010101", gunA, 39);
            remember("SAM-0001-00000000", gunA, 62);
            const int same = remembered("GSM-c0a4-01010101", gunA);
            const int otherTv = remembered("SAM-0001-00000000", gunA);
            const int otherGun = remembered("GSM-c0a4-01010101", gunB);
            remember("GSM-c0a4-01010101", gunB, 49);
            remember("GSM-c0a4-01010101", gunA, 30);
            const int thirdGun = remembered("GSM-c0a4-01010101", "16c0:0f02@0000:00:14.0-4");
            const int unknownTv = remembered("ABC-0000-00000000", gunA);
            check(empty && same == 39 && otherTv == 62 && otherGun == 39 && thirdGun == 30 &&
                      unknownTv == -1,
                  "each gun's exposure is kept for each TV; a gun with none on this TV starts at "
                  "the newest another gun was given on it; a TV never calibrated has none");
            cab::writeBytes(storage::configDir() + "/lightgun-cameras.json",
                            std::string("{\"GSM-c0a4-01010101\":{\"x\":900}}\n"));
            check(remembered("GSM-c0a4-01010101", "x") == -1 && remembered("GSM", "x") == -1,
                  "and a value off the ladder, or a file in another shape, is none");

            // ---- The gun's camera, on a tree laid out as sysfs has one: two
            // guns, each a hub with its camera on port 1 and the gun on port 2
            const std::string usb = root + "/sys/devices/pci0000:00/0000:c8:00.0/usb3/3-1";
            const std::string video = root + "/sys/class/video4linux";
            std::filesystem::create_directories(video, ec);
            auto camera = [&](const std::string& hub, const std::string& port,
                              const std::string& product, const std::string& node,
                              const std::string& index) {
                const std::string dev = usb + "/" + hub + "/" + hub + "." + port;
                cab::writeBytes(dev + "/product", product + "\n");
                const std::string iface = dev + "/" + hub + "." + port + ":1.0";
                const std::string at = iface + "/video4linux/" + node;
                cab::writeBytes(at + "/index", index + "\n");
                std::filesystem::create_directory_symlink(iface, at + "/device", ec);
                std::filesystem::create_directory_symlink(at, video + "/" + node, ec);
            };
            camera("3-1.2", "1", "SindenCamC", "video1", "0");
            camera("3-1.2", "1", "SindenCamC", "video2", "1");
            camera("3-1.3", "1", "SindenCamC", "video3", "0");
            camera("3-1.3", "1", "SindenCamC", "video4", "1");
            gSysVideo = video;
            gDevDir = "/dev";
            const std::string gun1 = realOf(usb) + "/3-1.2/3-1.2.2";
            const std::string gun2 = realOf(usb) + "/3-1.3/3-1.3.2";
            std::filesystem::create_directories(gun1, ec);
            std::filesystem::create_directories(gun2, ec);
            const std::string c1 = cameraOf(gun1), c2 = cameraOf(gun2);
            check(c1 == "/dev/video1" && c2 == "/dev/video3",
                  "each gun's camera is the SindenCam on its own hub, its capture node");
            camera("3-1.4", "1", "USB Camera", "video5", "0");
            std::filesystem::create_directories(realOf(usb) + "/3-1.4/3-1.4.2", ec);
            const std::string notSinden = cameraOf(realOf(usb) + "/3-1.4/3-1.4.2");
            camera("3-1.2", "3", "SindenCamC", "video6", "0");
            const std::string twoOnOne = cameraOf(gun1);
            check(notSinden.empty() && twoOnOne.empty() && cameraOf("").empty(),
                  "another camera on a hub is not a gun's, and with two on one hub none is "
                  "chosen");
            gSysVideo = "/sys/class/video4linux";
            std::filesystem::remove_all(root, ec);
        }
    }

    std::printf("%s\n", failures ? "gun camera test FAILED" : "gun camera test passed");
    return failures ? 1 : 0;
}

}  // namespace guncamera
