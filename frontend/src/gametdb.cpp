// A GameTDB controller list (gametdb.h). Moved out of wii.cpp and wiiu.cpp in
// the restructure (#398).

#include "gametdb.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>

namespace cab {

GameTdb::GameTdb(const char* envVar, const char* defaultPath, const char* tag, const char* missing)
    : envVar_(envVar), defaultPath_(defaultPath), tag_(tag), missing_(missing) {}

// Read once, on first use, from whichever thread asks first: the library pages
// in on workers and the launch path asks on the main thread.
void GameTdb::load() {
    const char* env = std::getenv(envVar_);
    const std::string path = env && *env ? env : defaultPath_;
    std::ifstream in(path);
    if (!in) {
        // Said, because without it every Wii game reads as needing a Remote,
        // which looks like a decision rather than a missing file. (Every Wii U
        // game, a GamePad.)
        std::fprintf(stderr, "[%s] no controller list at %s; %s\n", tag_, path.c_str(), missing_);
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        controls_[line.substr(0, sp)] = line.substr(sp + 1);
    }
    std::fprintf(stderr, "[%s] %zu games in the controller list\n", tag_, controls_.size());
}

std::string GameTdb::controls(const std::string& code) {
    if (code.empty()) return {};
    std::call_once(loaded_, [this] { load(); });
    std::set<char> letters;
    for (auto it = controls_.lower_bound(code);
         it != controls_.end() && it->first.compare(0, code.size(), code) == 0; ++it)
        letters.insert(it->second.begin(), it->second.end());
    return std::string(letters.begin(), letters.end());
}

}  // namespace cab
