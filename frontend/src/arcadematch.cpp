// Which emulator plays an arcade game, from the files in its zip; the rule is
// in arcadematch.h.

#include "arcadematch.h"

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

#include "catalog.h"

namespace arcadematch {

namespace {

// One set in one emulator's catalogue (cores/arcade-sets.sh's line).
struct Entry {
    bool works = true;
    char flag = '-';    // g a light gun, t a touchscreen
    std::vector<std::pair<uint32_t, int64_t>> files;   // CRC32 and size; size 0 = any
    std::vector<Disc> discs;                           // sha1 empty = by name only
};

struct Catalogue {
    std::unordered_map<std::string, Entry> sets;
    std::set<std::string> bios;   // Flycast's !bios line, in lower case
};

// THE ORDER, and it is the decision (arcadematch.h).
constexpr const char* kOrder[] = {"flycast", "fbneo_libretro", "mame2003_plus"};
constexpr size_t kCores = sizeof kOrder / sizeof kOrder[0];

std::string gDir = "cores/build";
std::once_flag gLoaded;
Catalogue gCat[kCores];

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The set's name and whether the file is a 7z, from `sf2.zip`.
void nameOf(const std::string& fileName, std::string* set, std::string* ext) {
    std::string base = fileName;
    if (const size_t slash = base.find_last_of('/'); slash != std::string::npos)
        base.erase(0, slash + 1);
    const size_t dot = base.find_last_of('.');
    *set = lower(dot == std::string::npos ? base : base.substr(0, dot));
    *ext = dot == std::string::npos ? std::string() : lower(base.substr(dot + 1));
}

Catalogue parse(const std::string& text) {
    Catalogue c;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> words;
        for (size_t i = 0; i < line.size();) {
            const size_t sp = line.find(' ', i);
            const size_t stop = sp == std::string::npos ? line.size() : sp;
            if (stop > i) words.push_back(line.substr(i, stop - i));
            i = stop + 1;
        }
        if (words.empty()) continue;
        if (words[0] == "!bios") {
            for (size_t i = 1; i < words.size(); ++i) c.bios.insert(lower(words[i]));
            continue;
        }
        if (words.size() < 3) continue;
        Entry e;
        e.works = words[1] == "w";
        e.flag = words[2][0];
        for (size_t i = 3; i < words.size(); ++i) {
            const std::string& w = words[i];
            const size_t colon = w.find(':');
            if (colon == std::string::npos) continue;
            if (w[0] == '@') {
                e.discs.push_back({lower(w.substr(1, colon - 1)), lower(w.substr(colon + 1))});
            } else {
                e.files.emplace_back(static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, 16)),
                                     std::strtoll(w.c_str() + colon + 1, nullptr, 10));
            }
        }
        c.sets.emplace(words[0], std::move(e));
    }
    return c;
}

void load() {
    std::call_once(gLoaded, [] {
        for (size_t i = 0; i < kCores; ++i) {
            std::string file = catalog::coreFileName(kOrder[i]);
            file.replace(file.size() - 3, 3, ".catalogue");   // fbneo_libretro.so
            const std::string path = gDir + "/" + file;
            std::string text;
            if (FILE* f = std::fopen(path.c_str(), "rb")) {
                char buf[65536];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
                std::fclose(f);
            }
            if (text.empty()) {
                std::fprintf(stderr, "[arcade] no catalogue at %s: %s plays nothing\n",
                             path.c_str(), kOrder[i]);
                continue;
            }
            gCat[i] = parse(text);
            std::fprintf(stderr, "[arcade] %s: %zu sets\n", kOrder[i], gCat[i].sets.size());
        }
    });
}

// The emulators this game is asked of, in order, with their entry for it.
struct Candidate {
    const char* core;
    const Entry* entry;
};
std::vector<Candidate> candidates(const Catalogue* cats, const std::string& fileName,
                                  bool dreamcast) {
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    std::vector<Candidate> out;
    for (size_t i = 0; i < kCores; ++i) {
        const std::string core = kOrder[i];
        if (dreamcast && core != "flycast") continue;
        if (ext == "7z" && core == "mame2003_plus") continue;
        const auto it = cats[i].sets.find(set);
        if (it == cats[i].sets.end() || !it->second.works) continue;
        // NOTHING LEFT TO CHECK is never a fit: an empty list fits any zip.
        if (it->second.files.empty() && it->second.discs.empty()) continue;
        out.push_back({kOrder[i], &it->second});
    }
    return out;
}

Answer controllerAnswer(const Candidate& c) {
    if (c.entry->flag == 'g') return {Verdict::NeedsGun, c.core, "Needs a light gun"};
    return {Verdict::NeedsTouch, c.core, "Needs a touchscreen"};
}

Answer notSupported() { return {Verdict::NotSupported, nullptr, "Not supported on this console"}; }

Answer byNameIn(const Catalogue* cats, const std::string& fileName, bool dreamcast) {
    const std::vector<Candidate> c = candidates(cats, fileName, dreamcast);
    if (c.empty()) return notSupported();
    if (c.front().entry->flag != '-') return controllerAnswer(c.front());
    return {Verdict::Plays, c.front().core, {}};
}

Answer byFilesIn(const Catalogue* cats, const std::string& fileName,
                 const std::vector<Member>& members, const std::vector<Disc>& discs,
                 bool dreamcast) {
    const std::vector<Candidate> c = candidates(cats, fileName, dreamcast);
    if (c.empty()) return notSupported();
    if (c.front().entry->flag != '-') return controllerAnswer(c.front());
    std::set<std::pair<uint32_t, int64_t>> have;
    std::set<uint32_t> crcs;
    for (const Member& m : members) {
        have.emplace(m.crc, m.size);
        crcs.insert(m.crc);
    }
    std::string base = fileName;
    if (const size_t slash = base.find_last_of('/'); slash != std::string::npos)
        base.erase(0, slash + 1);
    // The first emulator that has the set working says why, when none fits.
    Answer first;
    bool failed = false;
    auto fail = [&](Answer a) {
        if (!failed) first = std::move(a);
        failed = true;
    };
    for (const Candidate& k : c) {
        bool fits = true;
        for (const auto& f : k.entry->files)
            if (f.second > 0 ? !have.count(f) : !crcs.count(f.first)) { fits = false; break; }
        if (!fits) {
            fail({Verdict::FilesDiffer, k.core, "The files in " + base + " don't match its set"});
            continue;
        }
        bool missing = false, differs = false;
        for (const Disc& want : k.entry->discs) {
            const auto d = std::find_if(discs.begin(), discs.end(), [&](const Disc& x) {
                return lower(x.name) == want.name;
            });
            if (d == discs.end()) missing = true;
            else if (!want.sha1.empty() && lower(d->sha1) != want.sha1) differs = true;
        }
        if (missing) {
            fail({Verdict::DiscMissing, k.core, "Its disc image is missing"});
            continue;
        }
        if (differs) {
            fail({Verdict::DiscDiffers, k.core, "Its disc image doesn't match its set"});
            continue;
        }
        return {Verdict::Plays, k.core, {}};
    }
    return first;
}

uint32_t le32(const unsigned char* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const unsigned char* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint64_t le64(const unsigned char* p) {
    return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32);
}

bool readAt(FILE* f, int64_t at, size_t n, std::vector<unsigned char>* out) {
    out->assign(n, 0);
    return fseeko(f, at, SEEK_SET) == 0 && std::fread(out->data(), 1, n, f) == n;
}

// A ZIP'S OWN DIRECTORY, at its end: each file's name, CRC32 and size,
// without decompressing anything. ZIP64 for a zip past 4 GB or 65,535 files.
bool zipMembers(FILE* f, std::vector<Member>* out, std::string* err) {
    if (fseeko(f, 0, SEEK_END) != 0) return false;
    const int64_t size = ftello(f);
    const int64_t tail = std::min<int64_t>(size, 22 + 65535);
    std::vector<unsigned char> t;
    if (size < 22 || !readAt(f, size - tail, static_cast<size_t>(tail), &t)) {
        if (err) *err = "too short for a zip";
        return false;
    }
    int64_t eocd = -1;
    for (int64_t i = tail - 22; i >= 0; --i)
        if (le32(&t[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { if (err) *err = "no end of the zip's directory"; return false; }
    uint64_t entries = le16(&t[eocd + 10]);
    uint64_t dirSize = le32(&t[eocd + 12]);
    uint64_t dirAt = le32(&t[eocd + 16]);
    if (dirAt == 0xffffffffu || entries == 0xffff) {
        std::vector<unsigned char> loc, z;
        const int64_t locAt = size - tail + eocd - 20;
        if (locAt < 0 || !readAt(f, locAt, 20, &loc) || le32(loc.data()) != 0x07064b50 ||
            !readAt(f, static_cast<int64_t>(le64(&loc[8])), 56, &z) ||
            le32(z.data()) != 0x06064b50) {
            if (err) *err = "a ZIP64 directory that does not read";
            return false;
        }
        entries = le64(&z[32]);
        dirSize = le64(&z[40]);
        dirAt = le64(&z[48]);
    }
    std::vector<unsigned char> d;
    if (dirSize > 64u * 1024 * 1024 || !readAt(f, static_cast<int64_t>(dirAt), dirSize, &d)) {
        if (err) *err = "the zip's directory does not read";
        return false;
    }
    size_t p = 0;
    for (uint64_t n = 0; n < entries; ++n) {
        if (p + 46 > d.size() || le32(&d[p]) != 0x02014b50) {
            if (err) *err = "the zip's directory is damaged";
            return false;
        }
        const uint16_t nameLen = le16(&d[p + 28]), extraLen = le16(&d[p + 30]),
                       commentLen = le16(&d[p + 32]);
        if (p + 46 + nameLen + extraLen + commentLen > d.size()) {
            if (err) *err = "the zip's directory is damaged";
            return false;
        }
        Member m;
        m.crc = le32(&d[p + 16]);
        uint64_t usize = le32(&d[p + 24]);
        m.name.assign(reinterpret_cast<const char*>(&d[p + 46]), nameLen);
        if (usize == 0xffffffffu) {
            // The real size is the first field of its ZIP64 extra.
            for (size_t x = p + 46 + nameLen; x + 4 <= p + 46 + nameLen + extraLen;) {
                const uint16_t id = le16(&d[x]), len = le16(&d[x + 2]);
                if (id == 0x0001 && len >= 8) { usize = le64(&d[x + 4]); break; }
                x += 4 + len;
            }
        }
        m.size = static_cast<int64_t>(usize);
        if (!m.name.empty() && m.name.back() != '/') out->push_back(std::move(m));
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

// A 7z: every file decompressed through libarchive for its CRC32.
bool sevenZipMembers(const std::string& path, std::vector<Member>* out, std::string* err) {
    struct archive* a = archive_read_new();
    if (!a) return false;
    archive_read_support_format_7zip(a);
    if (archive_read_open_filename(a, path.c_str(), 1 << 16) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "does not open";
        archive_read_free(a);
        return false;
    }
    struct archive_entry* e = nullptr;
    std::vector<unsigned char> buf(1 << 16);
    bool ok = true;
    for (int r; (r = archive_read_next_header(a, &e)) != ARCHIVE_EOF;) {
        if (r != ARCHIVE_OK) { ok = false; break; }
        if (archive_entry_filetype(e) != AE_IFREG) continue;
        Member m;
        m.name = archive_entry_pathname(e) ? archive_entry_pathname(e) : "";
        uLong crc = crc32(0L, Z_NULL, 0);
        for (la_ssize_t n; (n = archive_read_data(a, buf.data(), buf.size())) != 0;) {
            if (n < 0) { ok = false; break; }
            crc = crc32(crc, buf.data(), static_cast<uInt>(n));
            m.size += n;
        }
        if (!ok) break;
        m.crc = static_cast<uint32_t>(crc);
        out->push_back(std::move(m));
    }
    if (!ok && err) *err = archive_error_string(a) ? archive_error_string(a) : "damaged";
    archive_read_free(a);
    return ok;
}

}  // namespace

bool isArcadeSlug(const std::string& slug) {
    for (const char* s : {"arcade", "cps1", "cps2", "cps3", "neogeomvs", "neogeoaes", "stv",
                          "model2", "model3"})
        if (slug == s) return true;
    return false;
}

bool asksDreamcast(const std::string& fileName) {
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    return ext == "zip" || ext == "7z";
}

void setDirectory(const std::string& dir) { gDir = dir; }

Answer byName(const std::string& fileName, bool dreamcast) {
    load();
    return byNameIn(gCat, fileName, dreamcast);
}

Answer byFiles(const std::string& fileName, const std::vector<Member>& members,
               const std::vector<Disc>& discs, bool dreamcast) {
    load();
    return byFilesIn(gCat, fileName, members, discs, dreamcast);
}

bool readMembers(const std::string& path, std::vector<Member>* out, std::string* err) {
    out->clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "does not open"; return false; }
    unsigned char magic[6] = {};
    const size_t n = std::fread(magic, 1, sizeof magic, f);
    bool ok = false;
    if (n >= 4 && magic[0] == 'P' && magic[1] == 'K') {
        ok = zipMembers(f, out, err);
        std::fclose(f);
    } else {
        std::fclose(f);
        static const unsigned char k7z[6] = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
        if (n == 6 && std::memcmp(magic, k7z, 6) == 0) ok = sevenZipMembers(path, out, err);
        else if (err) *err = "neither a zip nor a 7z";
    }
    return ok;
}

std::string chdSha1(const std::string& path) {
    unsigned char h[124] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    const size_t n = std::fread(h, 1, sizeof h, f);
    std::fclose(f);
    if (n < 100 || std::memcmp(h, "MComprHD", 8) != 0) return {};
    const uint32_t version = (h[12] << 24) | (h[13] << 16) | (h[14] << 8) | h[15];
    // MAME's chd.h: the SHA1 of the data and its metadata together.
    const size_t at = version == 3 ? 80 : version == 4 ? 48 : version == 5 ? 84 : 0;
    if (!at || at + 20 > n) return {};
    static const char* kHex = "0123456789abcdef";
    std::string out;
    for (size_t i = at; i < at + 20; ++i) {
        out += kHex[h[i] >> 4];
        out += kHex[h[i] & 15];
    }
    return out;
}

bool flycastBios(const std::string& fileName) {
    load();
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    return ext == "zip" && gCat[0].bios.count(set);
}

// ---- --arcade-match-test ----------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    // Three catalogues in the order Flycast, FinalBurn Neo, MAME 2003-Plus.
    Catalogue cats[kCores] = {
        parse("# flycast\n!bios naomi awbios\n"
              "azumanga w - b71ede16:0 @gdl-0018:\n"
              "hotd2 w g b23d1a0c:0\n"
              "tduno2 w t 1b58f24a:0\n"),
        parse("# fbneo\n"
              "sf2 w - 00000001:100 00000002:200\n"
              "gollygho n - 00000009:10\n"
              "opwolf w g 0000000a:10\n"
              "kinst w - 00000020:10\n"),
        parse("# mame2003_plus\n"
              "sf2 w - 00000001:100 00000003:300\n"
              "gollygho w - 00000009:10\n"
              "kinst w - 00000020:10 @kinst:aaaa\n"
              "saturn w -\n"
              "blockgal n - 00000030:10\n"),
    };
    auto name = [&](const char* f, bool dc = false) { return byNameIn(cats, f, dc); };
    auto files = [&](const char* f, std::vector<Member> m, std::vector<Disc> d = {},
                     bool dc = false) { return byFilesIn(cats, f, m, d, dc); };
    auto core = [](const Answer& a) { return std::string(a.core ? a.core : ""); };

    check(name("sf2.zip").verdict == Verdict::Plays && core(name("sf2.zip")) == "fbneo_libretro",
          "by name: sf2 is FinalBurn Neo's, the first in the order that has it");
    check(name("SF2.ZIP").verdict == Verdict::Plays, "the name is read in any case");
    check(name("nothere.zip").verdict == Verdict::NotSupported &&
              name("nothere.zip").why == "Not supported on this console",
          "a set no emulator has is not supported");
    check(core(name("gollygho.zip")) == "mame2003_plus",
          "an emulator that marks the set not working is skipped");
    check(name("blockgal.zip").verdict == Verdict::NotSupported,
          "a set every emulator marks not working is not supported");
    check(name("saturn.zip").verdict == Verdict::NotSupported,
          "an entry with nothing to check never fits");
    check(name("opwolf.zip").verdict == Verdict::NeedsGun &&
              name("opwolf.zip").why == "Needs a light gun",
          "a light-gun game needs a light gun");
    check(name("tduno2.zip").verdict == Verdict::NeedsTouch &&
              name("tduno2.zip").why == "Needs a touchscreen",
          "a touchscreen game needs a touchscreen");
    check(core(name("gollygho.7z")).empty(),
          "a 7z is never MAME 2003-Plus's, which cannot open one");
    check(core(name("azumanga.zip", true)) == "flycast",
          "in the Dreamcast folder, a Flycast set is Flycast's");
    check(name("sf2.zip", true).verdict == Verdict::NotSupported,
          "and any other zip there is not supported, never handed to Flycast");

    const std::vector<Member> fb = {{"a", 1, 100}, {"b", 2, 200}, {"extra", 7, 7}};
    const std::vector<Member> mame = {{"a", 1, 100}, {"c", 3, 300}};
    check(core(files("sf2.zip", fb)) == "fbneo_libretro",
          "by files: FinalBurn Neo's version, with an extra file, is FinalBurn Neo's");
    check(core(files("sf2.zip", mame)) == "mame2003_plus",
          "MAME 2003-Plus's version is MAME 2003-Plus's, though FinalBurn Neo comes first");
    check(files("sf2.zip", {{"a", 1, 100}, {"b", 2, 201}}).verdict == Verdict::FilesDiffer,
          "a file of the wrong size does not fit");
    {
        const Answer a = files("sf2.zip", {{"a", 1, 100}});
        check(a.verdict == Verdict::FilesDiffer && core(a) == "fbneo_libretro" &&
                  a.why == "The files in sf2.zip don't match its set",
              "fitting none, the first emulator that has the set says why");
    }
    check(core(files("azumanga.zip", {{"key", 0xb71ede16, 4096}}, {{"gdl-0018", "1234"}})) ==
              "flycast",
          "Flycast lists no sizes or disc SHA1: a CRC and a CHD of the name fit");
    check(files("azumanga.zip", {{"key", 0xb71ede16, 4096}}).verdict == Verdict::DiscMissing,
          "a set with a disc and no CHD beside it says its disc image is missing");
    check(core(files("kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "AAAA"}})) == "fbneo_libretro",
          "a set FinalBurn Neo has with no disc plays there");
    {
        Catalogue noFb[kCores] = {cats[0], Catalogue{}, cats[2]};
        check(core(byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"KINST", "AAAA"}}, false)) ==
                  "mame2003_plus",
              "a disc of the listed name and SHA1 fits, in any case");
        const Answer d =
            byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "bbbb"}}, false);
        check(d.verdict == Verdict::DiscDiffers && d.why == "Its disc image doesn't match its set",
              "a disc of another SHA1 does not fit");
    }
    check(files("hotd2.zip", {{"r", 0xb23d1a0c, 1}}).verdict == Verdict::NeedsGun,
          "by files too, a light-gun game needs a light gun");
    check(cats[0].bios.count("naomi") && cats[0].bios.count("awbios"),
          "Flycast's BIOS zips are read from its !bios line");

    std::printf("%s\n", failures ? "arcade match test: FAILED" : "arcade match test: passed");
    return failures ? 1 : 0;
}

}  // namespace arcadematch
