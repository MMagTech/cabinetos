// --ps3-install-test: what a PS3 game's folder holds after its packages are
// installed and what it starts on (standalone_rpcs3.cpp, standalone.cpp, #748
// and #749), through the calls a launch makes. In a scratch root, with no
// RPCS3, no server and no screen: a script stands in for bwrap, with RPCS3
// below it, and puts down what RPCS3 does for each kind of package.

#include "standalone.h"

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "disc.h"
#include "fileio.h"
#include "selftests.h"
#include "storage.h"

namespace {

constexpr int kGame = 5, kGameData = 4, kKey = 11;   // a package's kind, as its header says it

// RPCS3's log line decides an install, not its exit code, which is 134 for a
// good one (rpcs3Headless). A package's name says what the stand-in does.
constexpr const char* kStandIn = R"SH(#!/bin/sh
for a in "$@"; do
  case "$a" in XDG_CONFIG_HOME=*) home="${a#XDG_CONFIG_HOME=}";; esac
  pkg="$a"
done
name="${pkg##*/}"
echo "$name" >> "$PS3_TEST_RAN"
mkdir -p "$home/rpcs3"
case "$name" in
  refused*) echo "Failed to install" > "$home/rpcs3/RPCS3.log"; exit 134;;
  key*) mkdir -p "$home/hdd0/home/00000001/exdata"
        echo "the key" > "$home/hdd0/home/00000001/exdata/UP0001-NPUB00001_00-KEY0000000000001.edat";;
  game*) mkdir -p "$home/hdd0/game/NPUB00001/USRDIR"
         echo "sfo" > "$home/hdd0/game/NPUB00001/PARAM.SFO"
         echo "eboot" > "$home/hdd0/game/NPUB00001/USRDIR/EBOOT.BIN";;
esac
echo "Successfully installed $pkg (title_id=NPUB00001)" > "$home/rpcs3/RPCS3.log"
exit 134
)SH";

std::string text(const std::string& path) {
    const std::vector<uint8_t> b = cab::readBytes(path);
    return std::string(b.begin(), b.end());
}

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

int64_t size(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_size) : -1;
}

// A package's clear header, `kind` in the entry numbered 2 after one numbered
// 1, as every real one looked at has it, and `bytes` in all.
void package(const std::string& path, int kind, size_t bytes) {
    std::vector<uint8_t> b(bytes, 0);
    const auto word = [&b](size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (24 - 8 * i));
    };
    word(0, 0x7F504B47);
    word(8, 0xC0);
    word(12, 2);
    word(0xC0, 1), word(0xC4, 4), word(0xC8, 3);
    word(0xCC, 2), word(0xD0, 4), word(0xD4, static_cast<uint32_t>(kind));
    cab::writeBytes(path, b);
}

int count(const std::string& in, const std::string& what) {
    int n = 0;
    for (size_t at = 0; (at = in.find(what, at)) != std::string::npos; at += what.size()) ++n;
    return n;
}

}  // namespace

int ps3InstallTest() {
    using namespace cab::standalone;
    std::printf("what a PS3 game's folder holds after its install\n\n");
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    char made[] = "/tmp/cabinetos-ps3-install-XXXXXX";
    if (!mkdtemp(made)) {
        std::fprintf(stderr, "[ps3 install] could not make a scratch root\n");
        return 1;
    }
    const std::string root = made;
    setenv("CABINETOS_DRIVES", (root + "/drives").c_str(), 1);
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[ps3 install] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    const std::string ran = root + "/ran";
    cab::writeBytes(root + "/bin/bwrap", std::string(kStandIn));
    ::chmod((root + "/bin/bwrap").c_str(), 0755);
    const char* path = std::getenv("PATH");
    setenv("PATH", (root + "/bin:" + (path ? path : "/usr/bin:/bin")).c_str(), 1);
    setenv("PS3_TEST_RAN", ran.c_str(), 1);

    const Emulator& ps3 = *find("rpcs3");
    const Emulator& vita = *find("vita3k");
    // The PS3's system software, as installed.
    cab::writeBytes(home(ps3) + "/rpcs3/dev_flash/.cabinetos-firmware", std::string("there\n"));

    const std::string edat = "/UP0001-NPUB00001_00-KEY0000000000001.edat";
    const std::string eboot = "/NPUB00001/USRDIR/EBOOT.BIN";
    const std::string kept = "unlock keys kept\n";
    const std::atomic<bool> cancel{false};
    std::string rom, why;
    // A game's folder with its installed title and its note, and nothing else.
    auto before = [&](const std::string& name, const std::string& note) {
        const std::string d = root + "/cache/" + name;
        cab::writeBytes(d + "/NPUB00001/PARAM.SFO", std::string("sfo\n"));
        cab::writeBytes(d + eboot, std::string("eboot\n"));
        cab::writeBytes(d + "/.cabinetos-installed", note);
        ::unlink(ran.c_str());
        return d;
    };

    std::printf("a trial, its key and its licence\n");
    {
        const std::string d = root + "/cache/1 - Trial";
        package(d + "/game.pkg", kGame, 3 * 1024 * 1024);
        package(d + "/key.pkg", kKey, 6080);
        cab::writeBytes(d + "/UP0001-NPUB00001_00-KEY0000000000001.RAP", std::string("licence"));
        check(installGame(ps3, d, cancel, &rom, &why) && rom == d + eboot,
              "both install, and the game is what starts");
        check(text(d + edat) == "the key\n", "the key is in the game's folder");
        check(!exists(d + "/game.pkg") && !exists(d + "/key.pkg") && !exists(d + "/.install"),
              "the packages and the staging folder are gone");
        check(installedFile(ps3, d, "game.pkg", 3 * 1024 * 1024) &&
                  installedFile(ps3, d, "key.pkg", 6080),
              "and neither package is fetched again");
        bool missing = false;
        for (const char* who : {"/saves/one", "/saves/two"}) {
            const std::string save = root + who;
            check(prepare(ps3, d, rom, save, "Player", "Trial", 1, quality::Level{}, &missing,
                          &why) &&
                      text(save + "/hdd0/home/00000001/exdata" + edat) == "the key\n" &&
                      text(save + "/hdd0/home/00000001/exdata/"
                                  "UP0001-NPUB00001_00-KEY0000000000001.rap") == "licence",
                  who[7] == 'o' ? "the person playing gets the key and the licence"
                                : "and so does the second person");
            check(!exists(saveRoot(ps3, save) + edat),
                  "and the key is not among the saves sent to RomM");
        }
    }

    std::printf("a trial installed before keys were kept\n");
    {
        const std::string d = before("2 - Before", "3145728 game.pkg\n6080 key.pkg\n");
        check(installedFile(ps3, d, "game.pkg", 3 * 1024 * 1024), "its game is not fetched again");
        check(!installedFile(ps3, d, "key.pkg", 6080), "its key is");
        package(d + "/key.pkg", kKey, 6080);
        check(installGame(ps3, d, cancel, &rom, &why) && rom == d + eboot &&
                  text(d + edat) == "the key\n" && text(d + eboot) == "eboot\n",
              "the key is in the folder, the game as it was");
        check(text(d + "/.cabinetos-installed") == "3145728 game.pkg\n6080 key.pkg\n" + kept,
              "the note holds each once, and that the keys are kept");
        check(installedFile(ps3, d, "key.pkg", 6080), "and the key is not fetched a second time");
    }
    {
        const std::string d = before("3 - Fails", "3145728 game.pkg\n6080 refused key.pkg\n");
        package(d + "/refused key.pkg", kKey, 6080);
        check(installGame(ps3, d, cancel, &rom, &why) && rom == d + eboot,
              "a key that will not go in leaves the game playing as it did");
        check(!installedFile(ps3, d, "refused key.pkg", 6080), "and it is tried again next time");
    }

    std::printf("a small package that is not a key\n");
    {
        const std::string d = before("4 - Small", "3145728 game.pkg\n192336 refused patch.pkg\n");
        check(!installedFile(ps3, d, "refused patch.pkg", 192336), "is fetched once to be looked at");
        package(d + "/refused patch.pkg", kGameData, 192336);
        check(installGame(ps3, d, cancel, &rom, &why) && rom == d + eboot && !exists(ran),
              "is never installed twice, which RPCS3 refuses");
        check(!exists(d + "/refused patch.pkg") && !exists(d + edat) &&
                  installedFile(ps3, d, "refused patch.pkg", 192336),
              "is deleted, and not fetched again");
    }
    {
        const std::string d = root + "/cache/5 - Vita";
        cab::writeBytes(d + "/.cabinetos-installed", std::string("6080 game.zip\n"));
        check(installedFile(vita, d, "game.zip", 6080), "a Vita game's small archive never is");
    }
    {
        const std::string d = root + "/cache/6 - New";
        package(d + "/game.pkg", kGame, 2048);
        package(d + "/refused update.pkg", kGameData, 1024);
        check(!installGame(ps3, d, cancel, &rom, &why) && !exists(d + eboot) &&
                  exists(d + "/game.pkg") && !exists(d + "/.cabinetos-installed"),
              "a new game whose package will not go in is not installed, as before");
    }

    std::printf("what a game starts on\n");
    {
        const std::string d = before("7 - Disc", "1024 update.pkg\n");
        cab::writeBytes(d + "/Game.iso", std::string("disc"));
        check(bootPath(d) == d + "/Game.iso",
              "a disc game starts its disc, not an update installed beside it");
        check(bootPath(before("8 - Download", "")) == root + "/cache/8 - Download" + eboot,
              "a downloaded game starts its own program");
        const std::string v = root + "/cache/9 - Vita";
        cab::writeBytes(v + "/vita/ux0/app/PCSE00001/eboot.bin", std::string("vita"));
        check(bootPath(v) == v + "/vita/ux0/app/PCSE00001", "a Vita game starts its folder");
        check(bootPath(root + "/cache/none").empty(), "and an empty folder nothing");
    }

    std::printf("what comes down from RomM\n");
    {
        std::vector<romm::RomFile> f(5);
        const char* names[] = {"game.pkg", "key.pkg", "game.rap", "update.pkg", "song.pkg"};
        const char* kinds[] = {"game", "game", "game", "update", "dlc"};
        for (size_t i = 0; i < f.size(); ++i) {
            f[i].fileName = names[i];
            f[i].category = kinds[i];
            f[i].topLevel = i < 3;
        }
        std::string got;
        for (const romm::RomFile& g : disc::plan(f).fetch) got += g.fileName + " ";
        check(got == "game.pkg key.pkg game.rap ",
              "the game, its key and its licence; never an update or DLC");
    }
    check(count(text(root + "/cache/1 - Trial/.cabinetos-installed"), kept) == 1 &&
              size(root + "/cache/1 - Trial" + edat) == 8,
          "nothing above changed the first game");

    std::printf("\n%s\n", failures ? "FAILED" : "all of it as it should be");
    return failures ? 1 : 0;
}
