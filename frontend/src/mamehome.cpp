// MAME 0.289's home folder; what each part is for is in mamehome.h.

#include "mamehome.h"

#include <cstdio>
#include <vector>

#include "fileio.h"
#include "storage.h"

namespace mamehome {

void prepare(const std::string& saveDir, const std::string& coreDir, const std::string& biosDir) {
    const std::string home = biosDir + "/mame";
    storage::makeDirs(home + "/ini");
    const std::string plugins = coreDir + "/mame/plugins";
    if (!storage::exists(plugins + "/hiscore/init.lua"))
        std::fprintf(stderr, "[mame] no high-score plugin at %s\n", plugins.c_str());
    storage::pointAt(home + "/plugins", plugins);

    const std::string ini = home + "/ini/mame.ini";
    static const std::string kIni = "plugin hiscore\n";
    const std::vector<uint8_t> now = cab::readBytes(ini);
    if (std::string(now.begin(), now.end()) != kIni && !cab::writeBytes(ini, kIni))
        std::fprintf(stderr, "[mame] could not write %s\n", ini.c_str());

    const std::string scores = saveDir + "/mame/hiscore";
    storage::makeDirs(scores);
    if (storage::pointAt(home + "/hiscore", scores))
        std::fprintf(stderr, "[mame] high scores in %s\n", scores.c_str());
}

}  // namespace mamehome
