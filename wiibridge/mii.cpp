// SPDX-License-Identifier: GPL-2.0-or-later
//
// cabinetos-wii-bridge's Miis on a Remote (#275): the Remote's Mii memory,
// answered from one file per player light on the console. bridge.cpp's
// fromGame hands every read and write of that area here first. Moved out of
// bridge.cpp in the restructure (#398).

#include "bridge.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wiibridge {

// Where the Miis sent to a Remote are kept: `player-<light>.bin`, given by the
// console app as the first argument. Empty: none are kept.
std::string gMiiDir;

// --- Miis on a Remote (#275) ------------------------------------------------
//
// A WII KEEPS UP TO TEN MIIS ON A REMOTE, in its memory (EEPROM) at 0x0FCA to
// 0x15A9: two copies of one 752-byte block (WiiBrew, "Wiimote", EEPROM Memory).
// The Mii Channel's "send to Wii Remote" writes them there and games read them.
// Here they are kept on the console instead, one file per player light, local
// and not synced (MMagTech, 2026-10-06): reads and writes of that area are
// answered by the bridge from the file for the light the Remote shows, and the
// real Remote is never written. So a Mii sent while player 2 is player 2's,
// whoever holds that Remote next. A file nobody wrote reads as 0xFF, which is
// what MMagTech's TechKen copies hold there (A9, 2026-10-06).
constexpr unsigned kMiiStart = 0x0FCA;
constexpr unsigned kMiiEnd = 0x15AA;   // one past
constexpr size_t kMiiSize = kMiiEnd - kMiiStart;

// The player light the game gave this Remote (report 0x11: lights in the top
// four bits), 1 to 4; 0 when none or more than one is lit.
int playerLight(const Remote& r) {
    if (r.lastLed.size() < 2) return 0;
    switch (r.lastLed[1] & 0xf0) {
        case 0x10: return 1;
        case 0x20: return 2;
        case 0x40: return 3;
        case 0x80: return 4;
        default: return 0;
    }
}

std::string miiPath(int light) { return gMiiDir + "/player-" + std::to_string(light) + ".bin"; }

std::vector<uint8_t> miiLoad(int light) {
    std::vector<uint8_t> data(kMiiSize, 0xff);
    if (light == 0 || gMiiDir.empty()) return data;
    std::ifstream in(miiPath(light), std::ios::binary);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(kMiiSize));
    return data;
}

bool miiStore(int light, const std::vector<uint8_t>& data) {
    if (light == 0 || gMiiDir.empty()) return false;
    ::mkdir(gMiiDir.c_str(), 0755);
    const std::string path = miiPath(light);
    const std::string tmp = path + ".new";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// A READ OF THE MII AREA (0x17, EEPROM), answered from the file in 16-byte
// reports as a Remote answers, a little apart: a Remote's own come over
// Bluetooth about a millisecond apart, and the kernel keeps only 64 reports
// for a reader that falls behind. They go one at a time from the bridge's
// loop (miiAnswer), which holds this Remote till the last is out, so no other
// Remote waits for them (#480). Only a read wholly inside the area; one that
// crosses its edge goes to the Remote and is said in the log.
constexpr auto kMiiGap = std::chrono::milliseconds(1);

bool miiRead(Remote& r, const uint8_t* d, size_t n) {
    if (n < 7 || d[0] != 0x17 || (d[1] & 0x04)) return false;
    const unsigned addr = (unsigned(d[2]) << 16) | (unsigned(d[3]) << 8) | d[4];
    const unsigned size = (unsigned(d[5]) << 8) | d[6];
    if (size == 0 || addr + size <= kMiiStart || addr >= kMiiEnd) return false;
    if (addr < kMiiStart || addr + size > kMiiEnd) {
        std::fprintf(stderr, "[wiibridge] %s: a read of 0x%04x+%u crosses the Mii area; passed to the Remote\n",
                     r.uniq.c_str(), addr, size);
        return false;
    }
    const int light = playerLight(r);
    const std::vector<uint8_t> data = miiLoad(light);
    for (unsigned done = 0; done < size; done += 16) {
        const unsigned at = addr + done;
        const unsigned len = std::min(16u, size - done);
        uint8_t rep[22] = {0x21, r.btn1, r.btn2, static_cast<uint8_t>((len - 1) << 4),
                           static_cast<uint8_t>(at >> 8), static_cast<uint8_t>(at)};
        std::memcpy(rep + 6, data.data() + (at - kMiiStart), len);
        r.miiAnswers.left.emplace_back(rep, rep + sizeof rep);
    }
    std::fprintf(stderr, "[wiibridge] %s: Miis read for player %d (0x%04x+%u), from the console\n",
                 r.uniq.c_str(), light, addr, size);
    return true;
}

void miiAnswer(Remote& r) {
    Paced& p = r.miiAnswers;
    if (p.left.empty() || std::chrono::steady_clock::now() < p.due) return;
    const std::vector<uint8_t> rep = std::move(p.left.front());
    p.left.pop_front();
    toGame(r, rep.data(), rep.size());
    p.due = std::chrono::steady_clock::now() + kMiiGap;
}

// A WRITE TO THE MII AREA (0x16, EEPROM): into the file, acknowledged, and kept
// from the Remote, whatever part of it falls inside the area.
bool miiWrite(Remote& r, const uint8_t* d, size_t n) {
    if (n < 7 || d[0] != 0x16 || (d[1] & 0x04)) return false;
    const unsigned addr = (unsigned(d[2]) << 16) | (unsigned(d[3]) << 8) | d[4];
    const unsigned count = std::min<unsigned>(d[5], static_cast<unsigned>(n - 6));
    if (count == 0 || addr + count <= kMiiStart || addr >= kMiiEnd) return false;
    const int light = playerLight(r);
    std::vector<uint8_t> data = miiLoad(light);
    for (unsigned i = 0; i < count; ++i) {
        const unsigned at = addr + i;
        if (at >= kMiiStart && at < kMiiEnd) data[at - kMiiStart] = d[6 + i];
    }
    if (!miiStore(light, data))
        std::fprintf(stderr, "[wiibridge] %s: a Mii write with no player light (or no folder); dropped\n",
                     r.uniq.c_str());
    // Said once per block, as its last bytes go in, not per 16 bytes.
    if (addr + count > kMiiStart && (addr + count - kMiiStart) % 752 == 0)
        std::fprintf(stderr, "[wiibridge] %s: Miis written for player %d, kept on the console\n",
                     r.uniq.c_str(), light);
    ackToGame(r, 0x16);
    return true;
}

}  // namespace wiibridge
