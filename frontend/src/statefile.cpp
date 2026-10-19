#include "statefile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace statefile {

namespace {

constexpr char kMagic[] = "RASTATE";   // then the version byte
constexpr uint8_t kVersion = 1;

size_t aligned(size_t n) { return (n + 7) & ~static_cast<size_t>(7); }

void block(std::vector<uint8_t>& out, const char* name, const std::vector<uint8_t>& data) {
    const size_t at = out.size();
    out.resize(at + 8 + aligned(data.size()), 0);
    std::memcpy(out.data() + at, name, 4);
    const uint32_t n = static_cast<uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) out[at + 4 + i] = static_cast<uint8_t>(n >> (8 * i));
    if (!data.empty()) std::memcpy(out.data() + at + 8, data.data(), data.size());
}

}  // namespace

std::vector<uint8_t> join(const std::vector<uint8_t>& core,
                          const std::vector<uint8_t>& progress) {
    if (progress.empty()) return core;
    std::vector<uint8_t> out(8);
    std::memcpy(out.data(), kMagic, 7);
    out[7] = kVersion;
    // The core's block first, then the progress, as RetroArch writes them.
    block(out, "MEM ", core);
    block(out, "ACHV", progress);
    block(out, "END ", {});
    return out;
}

bool split(const std::vector<uint8_t>& file, Parts* out) {
    *out = Parts{};
    if (file.size() < 8 || std::memcmp(file.data(), kMagic, 7) != 0) {
        out->core = file.data();
        out->coreSize = file.size();
        return true;
    }
    out->wrapped = true;
    if (file[7] != kVersion) {
        std::fprintf(stderr, "[state] RetroArch's layout version %u, not 1: not loaded\n",
                     file[7]);
        return false;
    }
    // As RetroArch reads them (content_load_rastate1): a header must fit
    // before its length is read, a length must fit in what is left, and the
    // last block's padding may be missing.
    size_t at = 8;
    while (at + 8 <= file.size()) {
        const uint8_t* h = file.data() + at;
        const size_t n = static_cast<size_t>(h[4]) | static_cast<size_t>(h[5]) << 8 |
                         static_cast<size_t>(h[6]) << 16 | static_cast<size_t>(h[7]) << 24;
        at += 8;
        if (std::memcmp(h, "END ", 4) == 0) break;
        const size_t left = file.size() - at;
        if (n > left) {
            std::fprintf(stderr, "[state] block \"%.4s\" says %zu bytes, %zu left: not loaded\n",
                         reinterpret_cast<const char*>(h), n, left);
            return false;
        }
        if (std::memcmp(h, "MEM ", 4) == 0) {
            out->core = file.data() + at;
            out->coreSize = n;
        } else if (std::memcmp(h, "ACHV", 4) == 0) {
            out->progress = file.data() + at;
            out->progressSize = n;
        }
        at += std::min(aligned(n), left);
    }
    if (!out->core || out->coreSize == 0) {
        std::fprintf(stderr, "[state] RetroArch's layout with no core block: not loaded\n");
        return false;
    }
    return true;
}

}  // namespace statefile
