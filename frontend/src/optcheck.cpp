#include "optcheck.h"

#include <dirent.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "catalog.h"
#include "quality.h"
#include "sysopts.h"

namespace optcheck {

namespace {

// key -> accepted values, from one "key\tdefault\tv|v|v" file.
std::map<std::string, std::set<std::string>> readList(const std::string& path) {
    std::map<std::string, std::set<std::string>> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const size_t a = line.find('\t');
        if (a == std::string::npos) continue;
        const size_t b = line.find('\t', a + 1);
        std::set<std::string>& values = out[line.substr(0, a)];
        if (b == std::string::npos) continue;
        std::stringstream ss(line.substr(b + 1));
        std::string v;
        while (std::getline(ss, v, '|')) values.insert(v);
    }
    return out;
}

// The systems a core serves, where a table depends on the system.
std::vector<std::string> slugsFor(const std::string& core) {
    if (core == "dolphin") return {"ngc", "wii"};
    if (core == "beetle_vb") return {"virtualboy"};
    if (core == "gambatte") return {"gb", "gbc"};
    return {""};
}

// Options whose values are files the core finds on the console, so a listing
// made with an empty system folder shows none of them. Opera lists the BIOS
// images it finds; with none, only "disabled". Only the key is checked.
bool valuesAreFiles(const std::string& key) { return key == "opera_bios"; }

}  // namespace

int run(const std::string& dir) {
    std::vector<std::string> cores;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0)
                cores.push_back(n.substr(0, n.size() - 4));
        }
        ::closedir(d);
    }
    if (cores.empty()) {
        std::fprintf(stderr, "[optcheck] no option lists in %s\n", dir.c_str());
        return 1;
    }

    int bad = 0, checked = 0;
    for (const std::string& core : cores) {
        const auto declared = readList(dir + "/" + core + ".txt");
        // Everything the console can set on this core, with where it came from.
        std::vector<std::pair<std::pair<std::string, std::string>, std::string>> sets;
        for (const auto& kv : catalog::optionOverrides(core))
            sets.push_back({kv, "catalog"});
        for (const std::string& slug : slugsFor(core)) {
            for (int l = 0; l < quality::kLevelCount; ++l)
                for (bool vk : {true, false})
                    for (const auto& kv : quality::coreOptions(
                             core, slug, static_cast<quality::Level>(l), vk))
                        sets.push_back({kv, std::string("quality ") +
                                                quality::levelWord(
                                                    static_cast<quality::Level>(l))});
            for (const sysopts::Option& o : sysopts::forSystem(slug))
                for (const sysopts::Choice& c : o.choices)
                    for (const auto& kv : c.sets)
                        sets.push_back({{kv.first, kv.second}, std::string("pause menu ") + o.id});
        }
        for (const auto& [kv, from] : sets) {
            ++checked;
            auto it = declared.find(kv.first);
            if (it == declared.end()) {
                std::fprintf(stderr, "[optcheck] %s does not offer %s (%s sets it to %s)\n",
                             core.c_str(), kv.first.c_str(), from.c_str(), kv.second.c_str());
                ++bad;
            } else if (!it->second.empty() && !it->second.count(kv.second) &&
                       !valuesAreFiles(kv.first)) {
                std::fprintf(stderr, "[optcheck] %s: %s does not accept %s (%s)\n",
                             core.c_str(), kv.first.c_str(), kv.second.c_str(), from.c_str());
                ++bad;
            }
        }
    }
    std::fprintf(stderr, "[optcheck] %d values on %zu cores, %d that do not fit\n", checked,
                 cores.size(), bad);
    return bad == 0 ? 0 : 1;
}

}  // namespace optcheck
