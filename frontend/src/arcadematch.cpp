// Which emulator plays an arcade game, from the files in its zip; the rule is
// in arcadematch.h.

#include "arcadematch.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
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
#include "fileio.h"
#include "lindberghprograms.h"
#include "standalone.h"

namespace arcadematch {

namespace {

// One set in one emulator's catalogue (cores/arcade-sets.sh's line).
struct Entry {
    bool works = true;
    char flag = '-';    // g a light gun, x made for anything but a controller
    int buttons = 0;    // player 1's, where the catalogue says (b<n>)
    std::vector<std::pair<uint32_t, int64_t>> files;   // CRC32 and size; size 0 = any
    std::vector<Disc> discs;                           // sha1 empty = by name only
    // A Chihiro game's (=<ID>:<name>): the ID its image's boot.id carries,
    // and the name the emulator gives its saves.
    std::string id, name;
    // A Windows game's (~<prefix>:<verb>,...): its Wine prefix's last part
    // and the winetricks verbs the loader installs in it, in order.
    std::string prefix;
    std::vector<std::string> verbs;
    // Its picture options (%<KEY>=<default>:...).
    std::vector<WindowsOption> options;
};

struct Catalogue {
    std::unordered_map<std::string, Entry> sets;
    std::set<std::string> bios;   // Flycast's !bios line, in lower case
    // Linux Loader's !programs line: the names of the programs it starts, in
    // the order it looks for them (mainShared.c, games[]), as they are spelt.
    std::vector<std::string> programs;
    // windowsarcadeloader's files list, windowsarcadeloader.files.
    std::vector<WindowsFile> windowsFiles;
    // windowsarcadeloader's !runner line: the Wine folder it runs games under.
    std::string runner;
};

// THE ORDER, and it is the decision (arcadematch.h).
// sm2-emu, Supermodel, xemu-chihiro, Linux Loader and windowsarcadeloader are
// programs of their own (standalone.h), named as their catalog rows name them.
constexpr const char* kOrder[] = {"flycast",      "fbneo_libretro", "mame2003_plus",
                                  "sm2emu",       "supermodel",     "xemu-chihiro",
                                  "linuxloader",  "windowsarcadeloader",
                                  "mame_libretro"};
// The one that plays a converted image, `<set>.bin`, and nothing else.
constexpr const char* kImages = "xemu-chihiro";
// The one that plays a Lindbergh game's folder, by its program, and no zip.
constexpr const char* kFolders = "linuxloader";
// The one that plays a Windows arcade game's folder, by its .windowsloader
// file, and no zip.
constexpr const char* kWindows = "windowsarcadeloader";
constexpr size_t kCores = sizeof kOrder / sizeof kOrder[0];

// Where an emulator's catalogue is among the order's.
size_t indexOf(const char* core) {
    return static_cast<size_t>(std::find_if(std::begin(kOrder), std::end(kOrder),
                                            [core](const char* k) {
                                                return std::strcmp(k, core) == 0;
                                            }) -
                               std::begin(kOrder));
}

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
        if (words[0] == "!programs") {
            c.programs.assign(words.begin() + 1, words.end());
            continue;
        }
        if (words[0] == "!runner") {
            if (words.size() > 1) c.runner = words[1];
            continue;
        }
        if (words.size() < 3) continue;
        Entry e;
        e.works = words[1] == "w";
        e.flag = words[2][0];
        for (size_t i = 3; i < words.size(); ++i) {
            const std::string& w = words[i];
            const size_t colon = w.find(':');
            if (colon == std::string::npos) {
                if (w.size() > 1 && w[0] == 'b') e.buttons = std::atoi(w.c_str() + 1);
                continue;
            }
            if (w[0] == '%') {
                const size_t eq = w.find('=');
                if (eq == std::string::npos || eq > colon) continue;
                WindowsOption o;
                o.key = w.substr(1, eq - 1);
                o.def = w.substr(eq + 1, colon - eq - 1);
                const std::string range = w.substr(colon + 1);
                const size_t dash = range.find('-');
                const bool number =
                    dash != std::string::npos && dash > 0 && dash + 1 < range.size() &&
                    std::all_of(range.begin(), range.end(), [](char ch) {
                        return ch == '-' || std::isdigit(static_cast<unsigned char>(ch));
                    });
                if (number) {
                    o.min = std::atoi(range.c_str());
                    o.max = std::atoi(range.c_str() + dash + 1);
                } else {
                    for (size_t v = 0; v < range.size();) {
                        const size_t bar = std::min(range.find('|', v), range.size());
                        if (bar > v) o.choices.push_back(range.substr(v, bar - v));
                        v = bar + 1;
                    }
                }
                e.options.push_back(std::move(o));
            } else if (w[0] == '~') {
                e.prefix = w.substr(1, colon - 1);
                for (size_t v = colon + 1; v < w.size();) {
                    const size_t comma = std::min(w.find(',', v), w.size());
                    if (comma > v) e.verbs.push_back(w.substr(v, comma - v));
                    v = comma + 1;
                }
            } else if (w[0] == '=') {
                e.id = w.substr(1, colon - 1);
                e.name = w.substr(colon + 1);
            } else if (w[0] == '@') {
                // A space in a disc's name is written %20, and % as %25.
                std::string disc;
                for (size_t k = 1; k < colon; ++k) {
                    if (w[k] == '%' && k + 2 < colon && w.compare(k, 3, "%20") == 0) {
                        disc += ' ';
                        k += 2;
                    } else if (w[k] == '%' && k + 2 < colon && w.compare(k, 3, "%25") == 0) {
                        disc += '%';
                        k += 2;
                    } else {
                        disc += w[k];
                    }
                }
                e.discs.push_back({lower(disc), lower(w.substr(colon + 1))});
            } else {
                e.files.emplace_back(static_cast<uint32_t>(std::strtoul(w.c_str(), nullptr, 16)),
                                     std::strtoll(w.c_str() + colon + 1, nullptr, 10));
            }
        }
        c.sets.emplace(words[0], std::move(e));
    }
    return c;
}

bool isHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
               return std::isdigit(static_cast<unsigned char>(c)) || (c >= 'a' && c <= 'f');
           });
}

// windowsarcadeloader.files: `<verb> <cache folder>/<file name> <sha256>
// <url>` a line. The path is all between the verb and the SHA-256, so a
// space in a file's name stays in it; the URL is not kept (the console
// downloads nothing). A line of any other form is skipped.
std::vector<WindowsFile> parseFiles(const std::string& text) {
    std::vector<WindowsFile> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty() || line[0] == '#') continue;
        const size_t first = line.find(' ');
        const size_t last = line.find_last_of(' ');
        if (first == std::string::npos || last == first) continue;
        const size_t shaAt = line.find_last_of(' ', last - 1);
        if (shaAt == std::string::npos || shaAt <= first) continue;
        WindowsFile f;
        f.verb = line.substr(0, first);
        f.path = line.substr(first + 1, shaAt - first - 1);
        f.sha256 = lower(line.substr(shaAt + 1, last - shaAt - 1));
        if (f.verb.empty() || f.path.empty() || !isHex64(f.sha256)) continue;
        out.push_back(std::move(f));
    }
    return out;
}

// Every emulator's catalogue in `dir`, in the order. A missing one is said
// and reads as empty.
void loadFrom(const std::string& dir, Catalogue* cats) {
    for (size_t i = 0; i < kCores; ++i) {
        // fbneo_libretro.so's is fbneo_libretro.catalogue; a program of its
        // own has no .so, and its catalogue is named for it (sm2emu.catalogue,
        // supermodel.catalogue, xemu-chihiro.catalogue).
        std::string file = catalog::coreFileName(kOrder[i]);
        file.replace(file.size() - 3, 3, ".catalogue");
        if (cab::standalone::find(kOrder[i])) file = std::string(kOrder[i]) + ".catalogue";
        const std::string path = dir + "/" + file;
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
        cats[i] = parse(text);
        std::fprintf(stderr, "[arcade] %s: %zu sets\n", kOrder[i], cats[i].sets.size());
        if (std::strcmp(kOrder[i], kWindows) == 0) {
            const std::string list = dir + "/" + kWindows + ".files";
            const std::vector<uint8_t> bytes = cab::readBytes(list);
            cats[i].windowsFiles = parseFiles(std::string(bytes.begin(), bytes.end()));
            std::fprintf(stderr, "[arcade] %s: %zu Windows files\n", list.c_str(),
                         cats[i].windowsFiles.size());
        }
    }
}

void load() {
    std::call_once(gLoaded, [] { loadFrom(gDir, gCat); });
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
        // MAME 2003-Plus and Supermodel open a zip only.
        if (ext == "7z" && (core == "mame2003_plus" || core == "supermodel")) continue;
        // A CONVERTED IMAGE IS A CHIHIRO GAME, and a Chihiro game is one: no
        // other emulator opens a `.bin`, and xemu-chihiro opens nothing else.
        if ((ext == "bin") != (core == kImages)) continue;
        const auto it = cats[i].sets.find(set);
        if (it == cats[i].sets.end() || !it->second.works) continue;
        // NOTHING LEFT TO CHECK is never a fit: an empty list fits any zip.
        // So a Lindbergh program's line, which has none, is never a zip's:
        // `abc` is After Burner Climax's program and a MAME set.
        if (it->second.files.empty() && it->second.discs.empty() && it->second.id.empty())
            continue;
        out.push_back({kOrder[i], &it->second});
    }
    return out;
}

Answer notSupported() { return {Verdict::NotSupported, nullptr, "Not supported"}; }

// A GAME NOT MADE FOR A CONTROLLER, by any list that has it, is not
// supported, and a light-gun game, by any list, needs a light gun: what a game
// was made for does not change with the emulator, and an older list can call
// a gun a trackball (MAME 2003-Plus's Born to Fight).
bool marked(const std::vector<Candidate>& c, char flag) {
    for (const Candidate& k : c)
        if (k.entry->flag == flag) return true;
    return false;
}
bool noController(const std::vector<Candidate>& c) { return marked(c, 'x'); }
bool needsGun(const std::vector<Candidate>& c) { return marked(c, 'g'); }
Answer gunAnswer(const Candidate& c) { return {Verdict::NeedsGun, c.core, "Needs a light gun"}; }

Answer byNameIn(const Catalogue* cats, const std::string& fileName, bool dreamcast) {
    const std::vector<Candidate> c = candidates(cats, fileName, dreamcast);
    if (c.empty() || noController(c)) return notSupported();
    if (needsGun(c)) return gunAnswer(c.front());
    return {Verdict::Plays, c.front().core, {}};
}

Answer byFilesIn(const Catalogue* cats, const std::string& fileName,
                 const std::vector<Member>& members, const std::vector<Disc>& discs,
                 bool dreamcast) {
    const std::vector<Candidate> c = candidates(cats, fileName, dreamcast);
    if (c.empty() || noController(c)) return notSupported();
    if (needsGun(c)) return gunAnswer(c.front());
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

// A game by its key in one emulator's catalogue, the whole answer its line
// gives: one not there, not marked working or made for anything but a
// controller is not supported, a gun game needs a light gun.
Answer byKey(const Catalogue* cats, const char* core, const std::string& key) {
    const Catalogue& c = cats[indexOf(core)];
    const auto it = c.sets.find(key);
    if (it == c.sets.end() || !it->second.works || it->second.flag == 'x') return notSupported();
    if (it->second.flag == 'g') return {Verdict::NeedsGun, core, "Needs a light gun"};
    return {Verdict::Plays, core, {}};
}

// A Lindbergh game by a key of Linux Loader's catalogue: a program's name in
// lower case, or a fingerprint as eight hex digits.
Answer byLoaderKey(const Catalogue* cats, const std::string& key) {
    return byKey(cats, kFolders, key);
}

// A Windows game by its id, in windowsarcadeloader's catalogue.
Answer byWindowsIdIn(const Catalogue* cats, const std::string& gameId) {
    return byKey(cats, kWindows, gameId);
}

WindowsGame windowsGameIn(const Catalogue* cats, const std::string& gameId) {
    WindowsGame g;
    const Catalogue& c = cats[indexOf(kWindows)];
    const auto it = c.sets.find(gameId);
    if (it == c.sets.end()) return g;
    g.known = true;
    g.prefix = it->second.prefix;
    g.verbs = it->second.verbs;
    g.options = it->second.options;
    g.runner = c.runner;
    return g;
}

std::string fingerprintKey(uint32_t crc) {
    char k[9];
    std::snprintf(k, sizeof k, "%08x", crc);
    return k;
}

// Whether a file starts as a program does (ELF), which a data file named as a
// program does not.
bool isElf(const std::string& path) {
    unsigned char m[4] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const bool ok = std::fread(m, 1, 4, f) == 4 && std::memcmp(m, "\x7f" "ELF", 4) == 0;
    std::fclose(f);
    return ok;
}

// The folders in `dir`, by name, none hidden.
std::vector<std::string> foldersIn(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            struct stat st;
            const std::string p = dir + "/" + e->d_name;
            if (::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) out.push_back(p);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The program Linux Loader would start in `folder` by `programs`, its order:
// in the folder itself first, then in each folder one down by name, then two
// down (a zip that holds the game's own folder), the first of its names there
// that is a program.
std::string programIn(const std::vector<std::string>& programs, const std::string& folder) {
    std::vector<std::string> dirs = {folder};
    const std::vector<std::string> one = foldersIn(folder);
    dirs.insert(dirs.end(), one.begin(), one.end());
    for (const std::string& d : one) {
        const std::vector<std::string> two = foldersIn(d);
        dirs.insert(dirs.end(), two.begin(), two.end());
    }
    for (const std::string& dir : dirs)
        for (const std::string& name : programs) {
            struct stat st;
            const std::string p = dir + "/" + name;
            if (::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && isElf(p)) return p;
        }
    return {};
}

// The files in `dir`, by name, none hidden, that are `.windowsloader` files.
std::vector<std::string> loaderFilesIn(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.' || windowsGameId(e->d_name).empty()) continue;
            struct stat st;
            const std::string p = dir + "/" + e->d_name;
            if (::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) out.push_back(p);
        }
        ::closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

Answer byImageIn(const Catalogue* cats, const std::string& fileName, const std::string& id) {
    const std::vector<Candidate> c = candidates(cats, fileName, false);
    if (c.empty() || noController(c)) return notSupported();
    if (needsGun(c)) return gunAnswer(c.front());
    std::string base = fileName;
    if (const size_t slash = base.find_last_of('/'); slash != std::string::npos)
        base.erase(0, slash + 1);
    for (const Candidate& k : c)
        if (!id.empty() && k.entry->id == id) return {Verdict::Plays, k.core, {}};
    return {Verdict::FilesDiffer, c.front().core, base + " doesn't match its set"};
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
    for (const char* s : {"arcade", "mame", "mame-advmame", "fbneo", "fba", "naomi", "naomi2",
                          "naomigd", "atomiswave", "gaelco", "cps1", "cps2", "cps3", "neogeomvs",
                          "neogeoaes", "stv", "model2", "model3", "chihiro", "lindbergh",
                          "windows-arcade"})
        if (slug == s) return true;
    return false;
}

bool isWindowsFolder(const std::string& slug, const std::string& fsSlug) {
    return slug == "windows-arcade" || lower(fsSlug) == "windows-arcade";
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

Answer byImage(const std::string& fileName, const std::string& id) {
    load();
    return byImageIn(gCat, fileName, id);
}

Answer byProgram(const std::string& programName) {
    load();
    return byLoaderKey(gCat, lower(programName));
}

Answer byFingerprint(uint32_t crc) {
    load();
    return byLoaderKey(gCat, fingerprintKey(crc));
}

std::string lindberghProgram(const std::string& folder) {
    load();
    for (size_t i = 0; i < kCores; ++i)
        if (std::strcmp(kOrder[i], kFolders) == 0) return programIn(gCat[i].programs, folder);
    return {};
}

std::string lindberghProgramNamed(const std::vector<std::string>& fileNames) {
    load();
    for (size_t i = 0; i < kCores; ++i) {
        if (std::strcmp(kOrder[i], kFolders) != 0) continue;
        for (const std::string& p : gCat[i].programs)
            if (std::find(fileNames.begin(), fileNames.end(), p) != fileNames.end()) return p;
    }
    return {};
}

Answer byWindowsId(const std::string& gameId) {
    load();
    return byWindowsIdIn(gCat, gameId);
}

std::string windowsGameId(const std::string& path) {
    static const std::string kExt = ".windowsloader";
    const std::string base = path.substr(path.find_last_of('/') + 1);
    if (base.size() <= kExt.size() ||
        base.compare(base.size() - kExt.size(), kExt.size(), kExt) != 0)
        return {};
    return base.substr(0, base.size() - kExt.size());
}

std::string windowsIdNamed(const std::vector<std::string>& fileNames) {
    for (const std::string& n : fileNames)
        if (const std::string id = windowsGameId(n); !id.empty()) return id;
    return {};
}

std::string windowsLoaderFile(const std::string& folder) {
    std::vector<std::string> dirs = {folder};
    const std::vector<std::string> one = foldersIn(folder);
    dirs.insert(dirs.end(), one.begin(), one.end());
    for (const std::string& dir : dirs)
        if (const std::vector<std::string> f = loaderFilesIn(dir); !f.empty()) return f.front();
    return {};
}

WindowsGame windowsGame(const std::string& gameId) {
    load();
    return windowsGameIn(gCat, gameId);
}

std::vector<WindowsFile> windowsFiles() {
    load();
    return gCat[indexOf(kWindows)].windowsFiles;
}

bool lindberghFingerprint(const std::string& path, uint32_t* crc) {
    // As the loader reads it once the program is loaded (mainL.c, callback):
    // its third program header, when that is a loaded segment readable and
    // executable and nothing else (the code), 0x4000 bytes from 10 in. The
    // file holds those bytes at the segment's offset, as the program's
    // memory does; past the file's part of it, memory is zero.
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> h;
    bool ok = readAt(f, 0, 52, &h) && std::memcmp(h.data(), "\x7f" "ELF", 4) == 0 && h[4] == 1 &&
              h[5] == 1;
    std::vector<unsigned char> ph;
    if (ok) {
        const uint32_t phoff = le32(&h[0x1c]);
        const uint16_t phentsize = le16(&h[0x2a]), phnum = le16(&h[0x2c]);
        ok = phnum >= 3 && phentsize >= 32 && readAt(f, phoff + 2 * phentsize, 32, &ph) &&
             le32(&ph[0]) == 1 && le32(&ph[24]) == 5;
    }
    std::vector<unsigned char> code(0x4000, 0);
    if (ok) {
        const uint32_t offset = le32(&ph[4]), filesz = le32(&ph[16]);
        const uint32_t have = filesz > 10 ? std::min<uint32_t>(filesz - 10, 0x4000) : 0;
        std::vector<unsigned char> part;
        ok = have == 0 || readAt(f, static_cast<int64_t>(offset) + 10, have, &part);
        if (ok) std::copy(part.begin(), part.end(), code.begin());
    }
    std::fclose(f);
    if (!ok) return false;
    *crc = static_cast<uint32_t>(::crc32(0L, code.data(), static_cast<uInt>(code.size())));
    return true;
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

std::string chihiroId(const std::string& path) {
    // boot.id begins `BTID`, with `XBAM` 0x20 in and the game's ID at 0x30;
    // it sits on a 512-byte step within the image's first clusters (136 KB
    // in at most, in the six games read on 2026-11-25, #619).
    constexpr size_t kLook = 1 << 20;
    std::vector<unsigned char> head(kLook);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    const size_t n = std::fread(head.data(), 1, kLook, f);
    std::fclose(f);
    if (n < 16 || std::memcmp(head.data(), "FATX", 4) != 0) return {};
    for (size_t at = 512; at + 0x34 <= n; at += 512) {
        if (std::memcmp(&head[at], "BTID", 4) != 0 || std::memcmp(&head[at + 0x20], "XBAM", 4) != 0)
            continue;
        std::string id(reinterpret_cast<const char*>(&head[at + 0x30]), 4);
        for (const char c : id)
            if (!std::isalnum(static_cast<unsigned char>(c))) return {};
        return id;
    }
    return {};
}

std::string gameName(const char* core, const std::string& fileName) {
    load();
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    for (size_t i = 0; i < kCores; ++i) {
        if (std::strcmp(kOrder[i], core) != 0) continue;
        const auto it = gCat[i].sets.find(set);
        return it == gCat[i].sets.end() ? std::string() : it->second.name;
    }
    return {};
}

bool flycastBios(const std::string& fileName) {
    load();
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    return ext == "zip" && gCat[0].bios.count(set);
}

int buttons(const char* core, const std::string& fileName) {
    load();
    std::string set, ext;
    nameOf(fileName, &set, &ext);
    for (size_t i = 0; i < kCores; ++i) {
        if (std::strcmp(kOrder[i], core) != 0) continue;
        const auto it = gCat[i].sets.find(set);
        return it == gCat[i].sets.end() ? 0 : it->second.buttons;
    }
    return 0;
}

// ---- --arcade-changes ------------------------------------------------------------

int changes(const std::string& oldDir, const std::string& newDir) {
    Catalogue before[kCores], after[kCores];
    loadFrom(oldDir, before);
    loadFrom(newDir, after);
    std::set<std::string> names;
    for (size_t i = 0; i < kCores; ++i) {
        for (const auto& [n, e] : before[i].sets) names.insert(n);
        for (const auto& [n, e] : after[i].sets) names.insert(n);
    }
    // What a game shows for an answer: its emulator, or why it does not play.
    auto said = [](const Answer& a) {
        return a.verdict == Verdict::Plays ? std::string(a.core) : "(" + a.why + ")";
    };
    auto entryIn = [](const Catalogue* cats, const char* core, const std::string& n)
        -> const Entry* {
        for (size_t i = 0; i < kCores; ++i) {
            if (!core || std::strcmp(kOrder[i], core) != 0) continue;
            const auto it = cats[i].sets.find(n);
            return it == cats[i].sets.end() ? nullptr : &it->second;
        }
        return nullptr;
    };
    // The emulators that have the set working, in order: the ones its files
    // and its disc are tried on at Play.
    auto tried = [](const Catalogue* cats, const std::string& n) {
        std::string out;
        for (const Candidate& k : candidates(cats, n + ".zip", false))
            out += (out.empty() ? "" : ", ") + std::string(k.core);
        return out;
    };
    size_t moved = 0, refiled = 0, retried = 0;
    for (const std::string& n : names) {
        const Answer a = byNameIn(before, n + ".zip", false);
        const Answer b = byNameIn(after, n + ".zip", false);
        if (said(a) != said(b)) {
            std::printf("%s: %s -> %s\n", n.c_str(), said(a).c_str(), said(b).c_str());
            ++moved;
            continue;
        }
        if (b.verdict != Verdict::Plays) continue;
        // THE SAME BY NAME, BUT TRIED ON OTHERS AT PLAY: a zip or a disc the
        // first does not fit falls to the next (MAME 2003-Plus's Killer
        // Instinct with MAME 0.289's CHD).
        if (const std::string x = tried(before, n), y = tried(after, n); x != y) {
            std::printf("%s: %s by name; at Play tried on %s, was %s\n", n.c_str(), b.core,
                        y.c_str(), x.empty() ? "none" : x.c_str());
            ++retried;
            continue;
        }
        const Entry* x = entryIn(before, b.core, n);
        const Entry* y = entryIn(after, b.core, n);
        if (x && y && (x->files != y->files || x->discs.size() != y->discs.size() ||
                       !std::equal(x->discs.begin(), x->discs.end(), y->discs.begin(),
                                   [](const Disc& p, const Disc& q) {
                                       return p.name == q.name && p.sha1 == q.sha1;
                                   }))) {
            std::printf("%s: %s, its files changed\n", n.c_str(), b.core);
            ++refiled;
        }
    }
    std::printf("arcade changes: %zu sets change emulator, %zu may at Play, %zu keep theirs "
                "with other files, of %zu\n",
                moved, retried, refiled, names.size());
    return 0;
}

// ---- --arcade-match-test ----------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    // Nine catalogues in the order Flycast, FinalBurn Neo, MAME 2003-Plus,
    // sm2-emu, Supermodel, xemu-chihiro, Linux Loader, windowsarcadeloader,
    // MAME 0.289.
    Catalogue cats[kCores] = {
        parse("# flycast\n!bios naomi awbios\n"
              "azumanga w - b71ede16:0 @gdl-0018:\n"
              "hotd2 w g b23d1a0c:0\n"
              "tduno2 w x 1b58f24a:0\n"),
        parse("# fbneo\n"
              "sf2 w - 00000001:100 00000002:200\n"
              "gollygho n - 00000009:10\n"
              "opwolf w g 0000000a:10\n"
              "kinst w - 00000020:10\n"
              "vliner w x 00000040:10\n"
              "zombraid w - 00000060:10\n"),
        parse("# mame2003_plus\n"
              "sf2 w - 00000001:100 00000003:300\n"
              "gollygho w - 00000009:10\n"
              "kinst w - 00000020:10 @kinst:aaaa\n"
              "saturn w -\n"
              "blockgal n - 00000030:10\n"
              "vliner w - 00000040:10\n"
              "mjhaihai w x 00000050:10\n"
              "zombraid w g 00000060:10\n"
              "borntofi w - 00000080:10\n"),
        parse("# sm2emu\n"
              "daytona w - 00000101:0\n"
              "doa n - b3 00000102:0\n"
              "vf2 w - b3 00000103:0\n"
              "vcop n g 00000104:0\n"
              "lastbrnx n - b3 00000106:0\n"),
        parse("# supermodel\n"
              "scud w - 00000201:0\n"
              "vf3 w - b4 00000202:0\n"
              "lostwsga w g 00000203:0\n"),
        parse("# xemu-chihiro\n"
              "outr2 w - =SGBZ:outr2\n"
              "outr2st w - =SBJE:outr2st\n"
              "outr2sto w - =SBJE:outr2st\n"
              "vcop3 w g =SBFZ:vcop3\n"
              "mj2 n - =SBHC:mj2\n"
              "questofd n -\n"),
        parse("# linuxloader\n"
              "!programs a.elf abc gsevo hod4M.elf hummer_Master.elf mj4 vsg\n"
              "a.elf w -\n"
              "abc w -\n"
              "gsevo w g\n"
              "hod4m.elf w g\n"
              "hummer_master.elf w -\n"
              "mj4 w x\n"
              "vsg w g\n"
              "7129c32b w -\n"
              "e213414d w g\n"
              "057813a7 w x\n"
              "00000abc n -\n"),
        parse("# windowsarcadeloader\n!runner GE-Proton11-14-x86_64\n"
              "contra-evolution w - ~full:vcrun2008,d3dx9,dxvk "
              "%TP_RESOLUTION=1280x720:640x480|1280x720|1920x1080 %TP_RENDER_SCALE=1:1-4\n"
              "house-of-the-dead-scarlet-dawn w g ~full:vcrun2019\n"
              "music-gungun-2 n - ~full:\n"
              "mahjong-fight-club w x ~mfc:vcrun2005,dotnet11\n"
              "kinst w - ~full:\n"),
        parse("# mame_libretro\n"
              "sf2 w - b6 00000001:100 00000003:300\n"
              "gollygho w - b2 00000009:10\n"
              "kinst w - b6 00000020:10 @kinst:cccc\n"
              "raystorm w - b2 00000070:10 @raystorm:dddd\n"
              "roadburn w - b4 000000a0:10 @road%20burners%20v1.04:eeee\n"
              "borntofi w g b2 00000080:10\n"
              "megat w x 00000090:10\n"
              "daytona w - b9 00000101:10\n"
              "doa w - b3 00000102:10\n"
              "vcop w g b1 00000104:10\n"
              "lastbrnx n - b3 00000106:10\n"
              "scud n - b2 00000201:10\n"
              "vf3 n - b4 00000202:10\n"
              "lostwsga n g b2 00000203:10\n"
              "outr2 n - b6 00000300:80 @gdx-0004a:ffff\n"),
    };
    auto name = [&](const char* f, bool dc = false) { return byNameIn(cats, f, dc); };
    auto files = [&](const char* f, std::vector<Member> m, std::vector<Disc> d = {},
                     bool dc = false) { return byFilesIn(cats, f, m, d, dc); };
    auto core = [](const Answer& a) { return std::string(a.core ? a.core : ""); };

    check(name("sf2.zip").verdict == Verdict::Plays && core(name("sf2.zip")) == "fbneo_libretro",
          "by name: sf2 is FinalBurn Neo's, the first in the order that has it");
    check(name("SF2.ZIP").verdict == Verdict::Plays, "the name is read in any case");
    check(name("nothere.zip").verdict == Verdict::NotSupported &&
              name("nothere.zip").why == "Not supported",
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
    check(name("tduno2.zip").verdict == Verdict::NotSupported &&
              name("tduno2.zip").why == "Not supported",
          "a game played on its screen is not supported: no controller plays it");
    check(name("mjhaihai.zip").verdict == Verdict::NotSupported,
          "nor is a mahjong panel's game");
    check(name("vliner.zip").verdict == Verdict::NotSupported,
          "one list marking a game no controller's is enough, though another plays it");
    check(name("zombraid.zip").verdict == Verdict::NeedsGun,
          "and one list marking a light-gun game is enough, though the first plays it");
    check(core(name("gollygho.7z")) == "mame_libretro",
          "a 7z is never MAME 2003-Plus's, which cannot open one: the next that has it");
    check(core(name("raystorm.zip")) == "mame_libretro",
          "MAME 0.289 plays what nothing older has");
    check(core(name("gollygho.zip")) == "mame2003_plus",
          "and MAME 2003-Plus comes before it, where both have the set working");
    check(name("borntofi.zip").verdict == Verdict::NeedsGun,
          "a game MAME 0.289's list makes a light-gun game needs one, though an older list "
          "plays it");
    check(name("megat.zip").verdict == Verdict::NotSupported,
          "a touchscreen machine in MAME 0.289's list is not supported");
    check(cats[8].sets.at("raystorm").buttons == 2 && cats[1].sets.at("sf2").buttons == 0,
          "MAME 0.289's catalogue says how many buttons a game has; the others say none");
    check(core(name("daytona.zip")) == "sm2emu",
          "sm2-emu comes before MAME 0.289, where both have a Model 2 game working");
    check(core(name("doa.zip")) == "mame_libretro",
          "a game sm2-emu's list marks not working goes to the next that has it working");
    check(name("lastbrnx.zip").verdict == Verdict::NotSupported,
          "and one both mark not working is not supported");
    check(name("vcop.zip").verdict == Verdict::NeedsGun,
          "a light-gun game in sm2-emu's list needs a light gun");
    check(cats[3].sets.at("vf2").buttons == 3 && cats[3].sets.at("daytona").buttons == 0,
          "sm2-emu's catalogue gives a three-button game's buttons, and a driving game none");
    check(core(name("scud.zip")) == "supermodel",
          "Supermodel comes before MAME 0.289, which marks every Model 3 game not working");
    check(core(name("scud.7z")) == "",
          "a Model 3 7z is never Supermodel's, which opens a zip only, and MAME 0.289's is "
          "not working");
    check(name("lostwsga.zip").verdict == Verdict::NeedsGun,
          "a light-gun game in Supermodel's catalogue needs a light gun");
    check(cats[4].sets.at("vf3").buttons == 4 && cats[4].sets.at("scud").buttons == 0,
          "Supermodel's catalogue gives a fighting game's four buttons, and a driving game none");
    check(core(name("outr2.bin")) == "xemu-chihiro",
          "a converted Chihiro image, <set>.bin, is xemu-chihiro's");
    check(name("outr2.zip").verdict == Verdict::NotSupported,
          "a Chihiro set's zip is nobody's: MAME 0.289 marks it not working, and xemu-chihiro "
          "opens only the converted image");
    check(name("sf2.bin").verdict == Verdict::NotSupported,
          "a .bin is never another emulator's, though its name is one of their sets");
    check(name("vcop3.bin").verdict == Verdict::NeedsGun,
          "a Chihiro light-gun game needs a light gun");
    check(name("mj2.bin").verdict == Verdict::NotSupported &&
              name("questofd.bin").verdict == Verdict::NotSupported,
          "a Chihiro game the fork has no controls for is not supported, nor one it does not know");
    check(byImageIn(cats, "outr2.bin", "SGBZ").verdict == Verdict::Plays,
          "by its image, a game whose boot.id carries its set's ID plays");
    check(byImageIn(cats, "outr2sto.bin", "SBJE").verdict == Verdict::Plays &&
              cats[5].sets.at("outr2sto").name == "outr2st",
          "a revision carries its parent's ID, and its saves take its parent's name");
    {
        const Answer a = byImageIn(cats, "outr2.bin", "SBJE");
        check(a.verdict == Verdict::FilesDiffer && a.why == "outr2.bin doesn't match its set",
              "an image of another game under a set's name doesn't match its set");
    }
    {
        // A converted image's start: FATX, then boot.id on a 512-byte step.
        char tmpl[] = "/tmp/cabinetos-chihiro-XXXXXX";
        const int fd = mkstemp(tmpl);
        std::vector<unsigned char> img(0x20000, 0);
        std::memcpy(img.data(), "FATX", 4);
        std::memcpy(&img[0x15000], "BTID", 4);
        std::memcpy(&img[0x15020], "XBAM", 4);
        std::memcpy(&img[0x15030], "SBFZ", 4);
        const bool wrote = fd >= 0 && ::write(fd, img.data(), img.size()) ==
                                          static_cast<ssize_t>(img.size());
        if (fd >= 0) ::close(fd);
        check(wrote && chihiroId(tmpl) == "SBFZ", "an image's ID is read from its boot.id");
        img[0] = 'X';
        const int fd2 = ::open(tmpl, O_WRONLY | O_TRUNC);
        const bool wrote2 = fd2 >= 0 && ::write(fd2, img.data(), img.size()) ==
                                            static_cast<ssize_t>(img.size());
        if (fd2 >= 0) ::close(fd2);
        check(wrote2 && chihiroId(tmpl).empty(), "and a file that is not FATX has none");
        ::unlink(tmpl);
    }
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
        Catalogue noFb[kCores] = {cats[0], Catalogue{}, cats[2], cats[3], cats[4],
                                  cats[5], cats[6],    cats[7], cats[8]};
        check(core(byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"KINST", "AAAA"}}, false)) ==
                  "mame2003_plus",
              "a disc of the listed name and SHA1 fits, in any case");
        const Answer d =
            byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "bbbb"}}, false);
        check(d.verdict == Verdict::DiscDiffers && d.why == "Its disc image doesn't match its set",
              "a disc of another SHA1 does not fit");
        check(core(byFilesIn(noFb, "kinst.zip", {{"k", 0x20, 10}}, {{"kinst", "cccc"}}, false)) ==
                  "mame_libretro",
              "a disc made for MAME 0.289 plays there, though MAME 2003-Plus comes first");
    }
    check(core(files("raystorm.zip", {{"r", 0x70, 10}}, {{"raystorm", "dddd"}})) ==
              "mame_libretro",
          "by files, MAME 0.289's disc game with its CHD plays there");
    check(core(files("roadburn.zip", {{"r", 0xa0, 10}}, {{"Road Burners v1.04", "EEEE"}})) ==
              "mame_libretro",
          "and a disc whose name has spaces, written %20 in the catalogue");
    check(core(files("daytona.zip", {{"d", 0x101, 10}})) == "sm2emu",
          "by files, a Model 2 zip with the files both lists give plays in sm2-emu");
    check(core(files("vf3.zip", {{"v", 0x202, 10}})) == "supermodel",
          "by files, a Model 3 zip with the files its list gives plays in Supermodel");
    check(files("hotd2.zip", {{"r", 0xb23d1a0c, 1}}).verdict == Verdict::NeedsGun,
          "by files too, a light-gun game needs a light gun");
    check(files("vliner.zip", {{"v", 0x40, 10}}).verdict == Verdict::NotSupported,
          "and a game no controller plays is not supported, whatever its files");
    check(cats[0].bios.count("naomi") && cats[0].bios.count("awbios"),
          "Flycast's BIOS zips are read from its !bios line");
    check(isArcadeSlug("arcade") && isArcadeSlug("cps2") && isArcadeSlug("neogeomvs"),
          "RomM 5.4's arcade labels are arcade folders");
    check(isArcadeSlug("mame") && isArcadeSlug("fbneo") && isArcadeSlug("naomi") &&
              isArcadeSlug("naomigd") && isArcadeSlug("atomiswave") && isArcadeSlug("gaelco"),
          "and so are the labels RomM gives each emulator's folder after rommapp/romm#5273");
    check(isArcadeSlug("chihiro"), "and RomM's own label for Chihiro's folder");
    check(isArcadeSlug("lindbergh"), "and RomM's own label for Lindbergh's folder");
    check(isArcadeSlug("windows-arcade"), "and the Windows arcade games' folder");
    check(isWindowsFolder("windows-arcade", "windows-arcade") &&
              isWindowsFolder("arcade", "windows-arcade") &&
              isWindowsFolder("arcade", "Windows-Arcade") && !isWindowsFolder("arcade", "MAME") &&
              !isWindowsFolder("lindbergh", "Lindbergh"),
          "the Windows arcade folder known by its own name when RomM calls it arcade, and no "
          "other folder taken for it");
    check(!isArcadeSlug("triforce") && !isArcadeSlug("dc"),
          "a board no emulator here plays is not, nor is Dreamcast");

    // LINDBERGH: a game's folder, known by its program, never by a name.
    auto program = [&](const char* p) { return byLoaderKey(cats, lower(p)); };
    auto print = [&](uint32_t crc) { return byLoaderKey(cats, fingerprintKey(crc)); };
    check(program("hummer_Master.elf").verdict == Verdict::Plays &&
              core(program("hummer_Master.elf")) == "linuxloader",
          "Lindbergh, for a grid: Hummer Extreme's program plays in Linux Loader");
    check(program("hod4M.elf").verdict == Verdict::NeedsGun,
          "a light-gun game's program needs a light gun");
    check(program("mj4").verdict == Verdict::NotSupported,
          "a mahjong panel's program is not supported");
    check(program("segaboot").verdict == Verdict::NotSupported,
          "and a program the loader does not list is not supported");
    check(print(0x7129c32b).verdict == Verdict::Plays && core(print(0x7129c32b)) == "linuxloader",
          "at Play, by the loader's fingerprint: Hummer Extreme plays");
    check(print(0xe213414d).verdict == Verdict::NeedsGun && print(0x057813a7).verdict ==
              Verdict::NotSupported,
          "a light-gun game's fingerprint needs a light gun, a mahjong panel's is not supported");
    check(print(0xabc).verdict == Verdict::NotSupported &&
              print(0x12345678).verdict == Verdict::NotSupported,
          "a fingerprint the loader marks not working, or does not know, is not supported");
    check(core(name("abc.zip")) != "linuxloader",
          "a zip named as a Lindbergh program is never Linux Loader's");
    {
        // A folder as TeknoParrot lays one out: the program one folder down.
        char tmpl[] = "/tmp/cabinetos-lindbergh-XXXXXX";
        const bool made = ::mkdtemp(tmpl) != nullptr;
        const std::string top = tmpl;
        ::mkdir((top + "/elf").c_str(), 0755);
        ::mkdir((top + "/data").c_str(), 0755);
        auto touch = [&](const std::string& p, const std::vector<unsigned char>& b) {
            FILE* f = std::fopen(p.c_str(), "wb");
            if (!f) return false;
            const bool w = std::fwrite(b.data(), 1, b.size(), f) == b.size();
            std::fclose(f);
            return w;
        };
        // A 32-bit program whose third header is its code, at 0x1000, with
        // 0x5000 bytes of it in the file.
        std::vector<unsigned char> elf(0x6000, 0);
        std::memcpy(elf.data(), "\x7f" "ELF", 4);
        elf[4] = 1;
        elf[5] = 1;
        elf[0x1c] = 52;                    // e_phoff
        elf[0x2a] = 32;                    // e_phentsize
        elf[0x2c] = 3;                     // e_phnum
        const size_t third = 52 + 2 * 32;
        elf[third] = 1;                    // PT_LOAD
        elf[third + 5] = 0x10;             // p_offset 0x1000
        elf[third + 17] = 0x50;            // p_filesz 0x5000
        elf[third + 24] = 5;               // read and execute
        for (size_t i = 0x1000; i < 0x6000; ++i) elf[i] = static_cast<unsigned char>(i * 7);
        const bool wrote = made && touch(top + "/elf/hummer_Master.elf", elf) &&
                           touch(top + "/data/abc", {1, 2, 3});
        const uint32_t want =
            static_cast<uint32_t>(::crc32(0L, &elf[0x100a], static_cast<uInt>(0x4000)));
        uint32_t got = 0;
        check(wrote && programIn(cats[6].programs, top) == top + "/elf/hummer_Master.elf",
              "the program is found one folder down, by the loader's names, and a data file "
              "named as one is not it");
        check(wrote && lindberghFingerprint(top + "/elf/hummer_Master.elf", &got) && got == want,
              "and its fingerprint is CRC32 of 0x4000 bytes at its code's offset plus 10");
        check(!lindberghFingerprint(top + "/data/abc", &got),
              "a file that is no such program has none");
        const bool both = touch(top + "/elf/a.elf", elf);
        check(both && programIn(cats[6].programs, top) == top + "/elf/a.elf",
              "two programs in one folder: the first in the loader's own order");
        const bool upper = touch(top + "/vsg", elf);
        check(upper && programIn(cats[6].programs, top) == top + "/vsg",
              "and a program at the top is found before one below it");
        for (const char* f : {"/vsg", "/elf/a.elf", "/elf/hummer_Master.elf"})
            ::unlink((top + f).c_str());
        // A zip that held the game's own folder: the program two folders down.
        ::mkdir((top + "/Hummer").c_str(), 0755);
        ::mkdir((top + "/Hummer/elf").c_str(), 0755);
        const bool deep = touch(top + "/Hummer/elf/hummer_Master.elf", elf);
        check(deep && programIn(cats[6].programs, top) == top + "/Hummer/elf/hummer_Master.elf",
              "and one two folders down, in a zip that held the game's own folder");
        ::unlink((top + "/Hummer/elf/hummer_Master.elf").c_str());
        ::unlink((top + "/data/abc").c_str());
        for (const char* d : {"/Hummer/elf", "/Hummer", "/elf", "/data", ""})
            ::rmdir((top + d).c_str());
    }

    // WINDOWS ARCADE: a game's folder, known by its .windowsloader file's name.
    auto windows = [&](const std::string& id) { return byWindowsIdIn(cats, id); };
    check(windows("contra-evolution").verdict == Verdict::Plays &&
              core(windows("contra-evolution")) == "windowsarcadeloader",
          "Windows Arcade: a game the loader marks working, on a pad, plays in "
          "windowsarcadeloader");
    check(windows("Contra-Evolution").verdict == Verdict::NotSupported,
          "its id is read as the loader spells it: another case is not one of its games");
    check(windows("house-of-the-dead-scarlet-dawn").verdict == Verdict::NeedsGun &&
              windows("house-of-the-dead-scarlet-dawn").why == "Needs a light gun",
          "a gun game needs a light gun");
    check(windows("music-gungun-2").verdict == Verdict::NotSupported &&
              windows("music-gungun-2").why == "Not supported",
          "a game the loader does not mark working is not supported");
    check(windows("mahjong-fight-club").verdict == Verdict::NotSupported,
          "nor is one made for anything but a controller, though marked working");
    check(windows("not-a-loader-game").verdict == Verdict::NotSupported &&
              windows("").verdict == Verdict::NotSupported,
          "nor an id the loader does not have, nor no id (no .windowsloader file)");
    check(core(name("kinst.zip")) != "windowsarcadeloader" &&
              name("contra-evolution.zip").verdict == Verdict::NotSupported,
          "a zip named as a Windows game's id is never windowsarcadeloader's");
    {
        const WindowsGame g = windowsGameIn(cats, "mahjong-fight-club");
        check(g.known && g.prefix == "mfc" &&
                  g.verbs == std::vector<std::string>({"vcrun2005", "dotnet11"}),
              "a game's own prefix and its verbs are read back, in order");
        const WindowsGame shared = windowsGameIn(cats, "contra-evolution");
        check(shared.known && shared.prefix == "full" && shared.verbs.size() == 3 &&
                  shared.verbs[2] == "dxvk",
              "and the shared prefix's");
        const WindowsGame none = windowsGameIn(cats, "music-gungun-2");
        check(none.known && none.prefix == "full" && none.verbs.empty() && none.options.empty(),
              "a game with no verbs or options has none, and is still known");
        check(shared.options.size() == 2 && shared.options[0].key == "TP_RESOLUTION" &&
                  shared.options[0].def == "1280x720" &&
                  shared.options[0].choices ==
                      std::vector<std::string>({"640x480", "1280x720", "1920x1080"}),
              "a picture option that is a choice: its key, default and choices, in order");
        check(shared.options.size() == 2 && shared.options[1].key == "TP_RENDER_SCALE" &&
                  shared.options[1].def == "1" && shared.options[1].choices.empty() &&
                  shared.options[1].min == 1 && shared.options[1].max == 4,
              "and one that is a number: its default and range, no choices");
        check(shared.runner == "GE-Proton11-14-x86_64" && g.runner == shared.runner,
              "every game gives the catalogue's one runner");
        check(!windowsGameIn(cats, "not-a-loader-game").known, "an id not listed is not known");
    }
    {
        const std::string sha = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        const std::vector<WindowsFile> f = parseFiles(
            "# windowsarcadeloader files\n"
            "vcrun2019 vcrun2019/VC_redist.x64.exe " + sha + " https://example.invalid/a.exe\n"
            "d3dx9 directx9/directx_Jun2010_redist.exe " + sha + " https://example.invalid/b\n"
            "dotnet11 dotnet11/dotnet 1.1.exe " + sha + " https://example.invalid/c\n"
            "broken vcrun2019/x.exe nothex https://example.invalid/d\n");
        check(f.size() == 3 && f[0].verb == "vcrun2019" &&
                  f[0].path == "vcrun2019/VC_redist.x64.exe" && f[0].sha256 == sha &&
                  f[1].verb == "d3dx9" && f[1].path == "directx9/directx_Jun2010_redist.exe",
              "the files list is read back: each file's verb, cache path and SHA-256");
        check(f.size() == 3 && f[2].path == "dotnet11/dotnet 1.1.exe",
              "a space in a file's name stays in its path");
    }
    check(windowsIdNamed({"Daytona.exe", "contra-evolution.windowsloader", "x.windowsloader"}) ==
              "contra-evolution",
          "by RomM's list of a game's files: the first .windowsloader file's name");
    check(windowsIdNamed({"game.exe", "windowsloader", ".windowsloader", "x.WindowsLoader"})
              .empty(),
          "and none when no file is one, an ending in another case not one");
    check(windowsGameId("/g/Contra/Contra-Evolution.windowsloader") == "Contra-Evolution" &&
              windowsGameId("contra-evolution.windowsloader") == "contra-evolution" &&
              windowsGameId("/g/AMContra.exe").empty(),
          "a .windowsloader file's game id, from a path or a name, as it is spelt");
    {
        romm::RomFile zip;
        zip.fileName = "Contra Evolution.zip";
        zip.members = {{"Contra/AMContra.exe", 1, 10},
                       {"Contra/contra-evolution.windowsloader", 2, 10}};
        check(lindberghprograms::namedIn(lindberghprograms::Kind::Windows, {zip}) ==
                  "contra-evolution",
              "and a name inside a zip's members, by its last part");
    }
    {
        char tmpl[] = "/tmp/cabinetos-windows-XXXXXX";
        const bool made = ::mkdtemp(tmpl) != nullptr;
        const std::string top = tmpl;
        auto touch = [&](const std::string& p) {
            FILE* f = std::fopen(p.c_str(), "wb");
            if (!f) return false;
            const bool w = std::fputs("AMContra.exe\n", f) >= 0;
            std::fclose(f);
            return w;
        };
        check(made && windowsLoaderFile(top).empty(),
              "on the disk, a folder with no .windowsloader file has none");
        ::mkdir((top + "/Contra").c_str(), 0755);
        ::mkdir((top + "/Contra/deep").c_str(), 0755);
        const bool deep = touch(top + "/Contra/deep/deeper.windowsloader");
        check(deep && windowsLoaderFile(top).empty(),
              "and one two folders down is not the game's");
        const bool one = touch(top + "/Contra/contra-evolution.windowsloader");
        check(one && windowsLoaderFile(top) == top + "/Contra/contra-evolution.windowsloader" &&
                  windowsGameId(windowsLoaderFile(top)) == "contra-evolution",
              "one a folder down is found, a zip that held the game's own folder, and gives "
              "its id");
        const bool atTop = touch(top + "/contra-evolution-top.windowsloader") &&
                           touch(top + "/Other.WINDOWSLOADER");
        check(atTop && windowsLoaderFile(top) == top + "/contra-evolution-top.windowsloader",
              "and one at the top before it, a file whose ending is in another case passed "
              "over");
        for (const char* f : {"/contra-evolution-top.windowsloader", "/Other.WINDOWSLOADER",
                              "/Contra/contra-evolution.windowsloader",
                              "/Contra/deep/deeper.windowsloader"})
            ::unlink((top + f).c_str());
        for (const char* d : {"/Contra/deep", "/Contra", ""}) ::rmdir((top + d).c_str());
    }

    std::printf("%s\n", failures ? "arcade match test: FAILED" : "arcade match test: passed");
    return failures ? 1 : 0;
}

}  // namespace arcadematch
