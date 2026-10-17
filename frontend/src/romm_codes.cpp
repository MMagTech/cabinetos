// The codes RomM does not have: a Wii game's code and a Wii U game's product
// code, read off the game's file when RomM sends none (wii.h, wiiu.h), after
// every list of games the client fetches. Moved out of romm.cpp in the
// restructure (#398).

#include "romm.h"

#include "wii.h"
#include "wiiu.h"

#include <cctype>
#include <cstdio>

namespace romm {

void Client::fillWiiCodes(std::vector<Game>& games, size_t first) const {
    for (size_t i = first; i < games.size(); ++i) {
        Game& g = games[i];
        if (g.platformSlug == "wiiu") {
            fillWiiUCode(g);
            continue;
        }
        if (g.platformSlug != "wii" || !wii::codeFromTitleId(g.titleId).empty()) continue;
        bool known = false;
        std::string code = wii::rememberedCode(g.id, &known);
        if (!known) {
            const std::vector<uint8_t> head = fetchHead(g, wii::kHeaderBytes);
            // A failed read is not an answer: asked again next time.
            if (head.empty()) continue;
            code = wii::codeFromHeader(head.data(), head.size());
            wii::rememberCode(g.id, code);
            std::fprintf(stderr, "[wii] %s: RomM has no code; read %s off the file\n",
                         g.name.c_str(), code.empty() ? "none" : code.c_str());
        }
        if (!code.empty()) g.titleId = wii::titleIdOf(code);
    }
}

// A Wii U game's product code, which RomM does not have: remembered, else read
// off the end of its `.wua` in a few ranges (wiiu.h). Only a `.wua`: the format
// this console takes for Wii U.
void Client::fillWiiUCode(Game& g) const {
    bool known = false;
    std::string code = wiiu::rememberedCode(g.id, &known);
    if (!known) {
        const size_t dot = g.fsName.find_last_of('.');
        const bool wua = dot != std::string::npos && g.fsName.size() - dot == 4 &&
                         std::tolower(static_cast<unsigned char>(g.fsName[dot + 1])) == 'w' &&
                         std::tolower(static_cast<unsigned char>(g.fsName[dot + 2])) == 'u' &&
                         std::tolower(static_cast<unsigned char>(g.fsName[dot + 3])) == 'a';
        if (!wua || g.sizeBytes <= 0) return;
        bool failed = false;
        code = wiiu::codeFromWua(static_cast<uint64_t>(g.sizeBytes),
                                 [&](uint64_t off, size_t n) {
                                     std::vector<uint8_t> got = fetchRange(g, off, n);
                                     if (got.size() != n) failed = true;
                                     return got;
                                 });
        // A failed read is not an answer: asked again next time.
        if (failed && code.empty()) return;
        wiiu::rememberCode(g.id, code);
        std::fprintf(stderr, "[wiiu] %s: read %s off the file\n", g.name.c_str(),
                     code.empty() ? "no product code" : code.c_str());
    }
    g.productCode = code;
}

}  // namespace romm
