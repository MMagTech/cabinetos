#include "fileio.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>

namespace cab {
namespace {

bool makeParents(const std::string& path) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] != '/') continue;
        const std::string part = path.substr(0, i);
        if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

}  // namespace

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

std::string sanitisedStem(const std::string& title) {
    std::string out;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Anything above ASCII is passed through untouched. This is a UTF-8
        // string and those bytes are the tail of a multi-byte character: the
        // first version tested ASCII-only and turned "Pokémon" into "Pok__mon",
        // which is merely ugly — a Japanese title would have come out as
        // nothing but underscores, and a ROM library is full of those.
        if (u >= 0x80 || std::isalnum(u) || c == '-' || c == ' ' || c == '_' ||
            c == '(' || c == ')' || c == '.' || c == ',' || c == '\'') {
            out += c;
        } else {
            out += '_';
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "game" : out;
}

bool writeLocal(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}
