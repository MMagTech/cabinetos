#include "filesave.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>

namespace cab {
namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return suffix.size() <= s.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string parentOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool makeParents(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

// The VMU's own directory, blocks 253 down to 241, sixteen 32-byte entries to
// a 512-byte block. An entry's first byte says what occupies the slot: 0x33 a
// data file, 0xCC a game, 0x00 nothing.
bool vmuHoldsAFile(const std::vector<uint8_t>& d) {
    constexpr size_t kBlock = 512;
    constexpr size_t kCardBytes = 256 * kBlock;   // 131072
    // Anything that is not a whole 128 KB card is not a card this can read,
    // and refusing is the safe direction: the byte scan below would be reading
    // past the end of something else.
    if (d.size() != kCardBytes) return false;
    // The root block's signature, sixteen 0x55 bytes. Every one of the
    // thirteen real cards on the reference server carries it.
    const size_t root = 255 * kBlock;
    for (size_t i = 0; i < 16; ++i)
        if (d[root + i] != 0x55) return false;
    for (size_t blk = 241; blk <= 253; ++blk) {
        const size_t off = blk * kBlock;
        for (size_t e = 0; e < 16; ++e) {
            const uint8_t kind = d[off + e * 32];
            if (kind == 0x33 || kind == 0xCC) return true;
        }
    }
    return false;
}

bool anyRealByte(const std::vector<uint8_t>& d, size_t skipFront, size_t skipBack) {
    if (d.size() <= skipFront + skipBack) return false;
    for (size_t i = skipFront; i < d.size() - skipBack; ++i)
        if (d[i] != 0x00 && d[i] != 0xFF) return true;
    return false;
}

}  // namespace

bool holdsASave(const std::vector<uint8_t>& d, catalog::Untouched rule) {
    if (d.empty()) return false;
    switch (rule) {
        case catalog::Untouched::Blank:
            return anyRealByte(d, 0, 0);
        case catalog::Untouched::Uniform:
            return std::any_of(d.begin(), d.end(),
                               [first = d.front()](uint8_t b) { return b != first; });
        case catalog::Untouched::SegaCDBackup:
            return anyRealByte(d, 16, 64);
        case catalog::Untouched::ThreeDONvram:
            return anyRealByte(d, 176, 0);
        case catalog::Untouched::VmuDirectory:
            return vmuHoldsAFile(d);
    }
    return anyRealByte(d, 0, 0);
}

std::string writtenFile(const catalog::SaveFile& spec, const std::string& path) {
    struct stat st;
    if (spec.captureSuffix.empty())
        return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? path : std::string();

    // The predicted name first. A scan is only ever a fallback, because it
    // cannot tell two files apart if a core ever writes more than one.
    if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return path;

    const std::string dir = parentOf(path);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return std::string();
    std::string found;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        if (!endsWith(name, spec.captureSuffix)) continue;
        // The exclusion is the whole of what keeps Sega CD's two regions
        // apart: `4Mbit_cart.brm` also ends in `.brm`, and without this the
        // cartridge would be captured as the internal backup RAM and then
        // restored over it.
        if (!spec.captureExclude.empty() && endsWith(name, spec.captureExclude)) continue;
        const std::string cand = dir + "/" + name;
        if (::lstat(cand.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        // Sorted by name so a directory holding two matches answers the same
        // way twice rather than however readdir felt.
        if (found.empty() || cand < found) found = cand;
    }
    ::closedir(d);
    return found;
}

std::vector<uint8_t> readBytes(const std::string& path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

bool writeBytes(const std::string& path, const std::vector<uint8_t>& data) {
    if (!makeParents(path)) return false;
    const std::string tmp = path + ".placing";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool wrote = data.empty() ||
                       std::fwrite(data.data(), 1, data.size(), f) == data.size();
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool removeFile(const std::string& path) {
    if (::unlink(path.c_str()) == 0) return true;
    return errno == ENOENT;
}

}  // namespace cab
