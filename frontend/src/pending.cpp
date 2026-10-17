// The markers of what this console still owes RomM (pending.h). They hold no
// state in memory, which is what lets clearPending run on the uploader's
// worker while owed and isPending run on the main thread. Moved out of
// cache.cpp in the restructure (#398).

#include "pending.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "storage.h"

namespace cache {
namespace {

// A file name that cannot escape its directory or collide with a marker from
// another game. Upload file names come from game titles, which contain slashes,
// colons and worse.
std::string markerName(int romId, const std::string& fileName) {
    std::string s = std::to_string(romId) + "-";
    for (unsigned char c : fileName) {
        s += (std::isalnum(c) || c == '.' || c == '-' || c == '_')
                 ? static_cast<char>(c) : '_';
    }
    return s + ".pending";
}

}  // namespace

void markPending(const storage::User& u, const Owed& o, int64_t bytes) {
    if (!u.valid()) return;
    storage::makeDirs(storage::pendingDir(u));
    const std::string path = storage::pendingDir(u) + "/" + markerName(o.romId, o.fileName);
    // One "key value" per line after the byte count. Values are file names
    // and paths, which never hold a newline.
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "%lld\n", static_cast<long long>(bytes));
        std::fprintf(f, "kind %s\n", o.isScreenshot ? "screenshot" : o.isState ? "state" : "save");
        std::fprintf(f, "rom %d\n", o.romId);
        std::fprintf(f, "emulator %s\n", o.emulator.c_str());
        std::fprintf(f, "name %s\n", o.fileName.c_str());
        std::fprintf(f, "local %s\n", o.localPath.c_str());
        if (!o.shotName.empty()) {
            std::fprintf(f, "shotname %s\n", o.shotName.c_str());
            std::fprintf(f, "shot %s\n", o.shotPath.c_str());
        }
        std::fclose(f);
    }
}

std::vector<Owed> owed(const storage::User& u) {
    std::vector<std::pair<time_t, Owed>> found;
    if (!u.valid()) return {};
    const std::string dir = storage::pendingDir(u);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return {};
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string path = dir + "/" + e->d_name;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        Owed o;
        bool first = true, kind = false;
        char line[4096];
        while (std::fgets(line, sizeof line, f)) {
            std::string l(line);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (first) { first = false; continue; }   // the byte count
            const size_t sp = l.find(' ');
            if (sp == std::string::npos) continue;
            const std::string k = l.substr(0, sp), v = l.substr(sp + 1);
            if (k == "kind") {
                kind = true;
                o.isState = v == "state";
                o.isScreenshot = v == "screenshot";
            }
            else if (k == "rom") o.romId = std::atoi(v.c_str());
            else if (k == "emulator") o.emulator = v;
            else if (k == "name") o.fileName = v;
            else if (k == "local") o.localPath = v;
            else if (k == "shotname") o.shotName = v;
            else if (k == "shot") o.shotPath = v;
        }
        std::fclose(f);
        if (!kind || o.romId <= 0 || o.emulator.empty() || o.fileName.empty() ||
            o.localPath.empty()) {
            std::fprintf(stderr, "[upload] %s says nothing about what to send; left owed\n",
                         e->d_name);
            continue;
        }
        struct stat st;
        found.emplace_back(::stat(path.c_str(), &st) == 0 ? st.st_mtime : 0, std::move(o));
    }
    ::closedir(d);
    std::stable_sort(found.begin(), found.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Owed> out;
    for (auto& [t, o] : found) out.push_back(std::move(o));
    return out;
}

void clearPending(const storage::User& u, int romId, const std::string& fileName) {
    if (!u.valid()) return;
    ::unlink((storage::pendingDir(u) + "/" + markerName(romId, fileName)).c_str());
}

bool isPending(const storage::User& u, int romId, const std::string& fileName) {
    if (!u.valid()) return false;
    struct stat st;
    const std::string path = storage::pendingDir(u) + "/" + markerName(romId, fileName);
    return ::stat(path.c_str(), &st) == 0;
}

int pendingCount(const storage::User& u) {
    if (!u.valid()) return 0;
    DIR* d = ::opendir(storage::pendingDir(u).c_str());
    if (!d) return 0;
    int n = 0;
    while (struct dirent* f = ::readdir(d))
        if (f->d_name[0] != '.') ++n;
    ::closedir(d);
    return n;
}

int64_t pendingBytes() {
    int64_t total = 0;
    for (const storage::User& u : storage::knownUsers()) {
        const std::string dir = storage::pendingDir(u);
        DIR* d = ::opendir(dir.c_str());
        if (!d) continue;
        while (struct dirent* f = ::readdir(d)) {
            if (f->d_name[0] == '.') continue;
            if (FILE* fh = std::fopen((dir + "/" + f->d_name).c_str(), "rb")) {
                long long n = 0;
                if (std::fscanf(fh, "%lld", &n) == 1 && n > 0) total += n;
                std::fclose(fh);
            }
        }
        ::closedir(d);
    }
    return total;
}

}  // namespace cache
