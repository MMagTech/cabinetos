// The codes read off game files (codecache.h). Moved out of wii.cpp and
// wiiu.cpp in the restructure (#398).

#include "codecache.h"

#include <fstream>

#include "storage.h"

namespace cab {

CodeCache::CodeCache(const char* fileName) : fileName_(fileName) {}

std::string CodeCache::path() const { return storage::configDir() + "/" + fileName_; }

// Called with lock_ held.
void CodeCache::read() {
    if (read_) return;
    read_ = true;
    std::ifstream in(path());
    int id;
    std::string code;
    while (in >> id >> code) codes_[id] = code == "-" ? std::string() : code;
}

std::string CodeCache::remembered(int romId, bool* known) {
    std::lock_guard<std::mutex> lock(lock_);
    read();
    auto it = codes_.find(romId);
    *known = it != codes_.end();
    return *known ? it->second : std::string();
}

void CodeCache::remember(int romId, const std::string& code) {
    std::lock_guard<std::mutex> lock(lock_);
    read();
    codes_[romId] = code;
    std::ofstream out(path(), std::ios::app);
    out << romId << ' ' << (code.empty() ? "-" : code) << '\n';
}

}  // namespace cab
