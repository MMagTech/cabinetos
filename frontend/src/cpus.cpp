#include "cpus.h"

#include <sched.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

namespace cpus {
namespace {

bool gChecked = false;
bool gReady = false;     // gAll was read; children can be put back
bool gPlace = false;     // this machine has enough fast cores to use them
bool gPlaced = false;    // the game thread is on them now
cpu_set_t gAll;
cpu_set_t gFast;

std::string readLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

// A fast core's top clock is within a tenth of the highest. Wide enough for
// the few hundred MHz a "favoured" core gets over its fast siblings (Intel's
// Turbo Boost Max, the A9's own 196-to-208 spread), far from the slow cores,
// which on the A9 top out at 64% and on Intel's E cores at about 70%.
constexpr long kFastPercent = 90;
// Fewer fast cores than this and a game is better left on all of them.
constexpr int kMinFastCores = 4;

void check() {
    if (gChecked) return;
    gChecked = true;
    CPU_ZERO(&gAll);
    CPU_ZERO(&gFast);
    if (sched_getaffinity(0, sizeof(gAll), &gAll) != 0) {
        std::fprintf(stderr, "[cpus] cannot read which cores this console may use\n");
        return;
    }
    gReady = true;

    long top = 0;
    long speed[CPU_SETSIZE] = {};
    for (int c = 0; c < CPU_SETSIZE; ++c) {
        if (!CPU_ISSET(c, &gAll)) continue;
        const std::string s =
            readLine("/sys/devices/system/cpu/cpu" + std::to_string(c) + "/cpufreq/cpuinfo_max_freq");
        speed[c] = s.empty() ? 0 : std::atol(s.c_str());
        if (speed[c] <= 0) {
            std::fprintf(stderr, "[cpus] cannot read core %d's top speed; games use every core\n", c);
            return;
        }
        if (speed[c] > top) top = speed[c];
    }

    int all = 0, fast = 0;
    std::set<std::string> fastCores;   // one entry per physical core
    std::string list;
    for (int c = 0; c < CPU_SETSIZE; ++c) {
        if (!CPU_ISSET(c, &gAll)) continue;
        ++all;
        if (speed[c] * 100 < top * kFastPercent) continue;
        ++fast;
        CPU_SET(c, &gFast);
        const std::string topo = "/sys/devices/system/cpu/cpu" + std::to_string(c) + "/topology/";
        std::string siblings = readLine(topo + "core_cpus_list");
        if (siblings.empty()) siblings = readLine(topo + "thread_siblings_list");
        fastCores.insert(siblings.empty() ? std::to_string(c) : siblings);
        if (!list.empty()) list += ',';
        list += std::to_string(c);
    }
    if (fast == all) {
        std::fprintf(stderr, "[cpus] all %d cores are alike; games use every core\n", all);
        return;
    }
    if (static_cast<int>(fastCores.size()) < kMinFastCores) {
        std::fprintf(stderr, "[cpus] only %zu fast cores of %d; games use every core\n",
                     fastCores.size(), all);
        return;
    }
    gPlace = true;
    std::fprintf(stderr, "[cpus] %zu fast cores (%s, %.2f GHz) of %d threads; games run there\n",
                 fastCores.size(), list.c_str(), top / 1e6, all);
}

}  // namespace

void gameStart() {
    check();
    if (!gPlace || gPlaced) return;
    // sched_setaffinity on 0 is the calling thread only, which is the point.
    if (sched_setaffinity(0, sizeof(gFast), &gFast) != 0) {
        std::fprintf(stderr, "[cpus] could not move the game to the fast cores\n");
        return;
    }
    gPlaced = true;
    std::fprintf(stderr, "[cpus] game on the fast cores\n");
}

void gameEnd() {
    if (!gPlaced) return;
    sched_setaffinity(0, sizeof(gAll), &gAll);
    gPlaced = false;
    std::fprintf(stderr, "[cpus] back on every core\n");
}

void childOnAllCores() {
    if (gReady) sched_setaffinity(0, sizeof(gAll), &gAll);
}

}  // namespace cpus
