// What to hand a core, given whatever the server actually had.
//
// A RomM server holds whatever its owner put in it, and that is not uniform.
// Measured on the reference library, 1644 games: 801 `.zip`, 237 `.chd`,
// 84 `.7z`, a long tail of plain uncompressed ROMs — `.a78`, `.jag`, `.vb`,
// `.nds`, `.sfc`, `.nes`, `.md`, `.gb` — and **32 files with no extension at
// all**. Any of those is a normal thing for someone to own.
//
// Three rules come out of that, and each one rules out an obvious shortcut.
//
// NEVER DISPATCH ON THE FILE EXTENSION. Thirty-two files in one library do not
// have one, and a name is metadata a server happens to carry rather than a fact
// about the bytes. The codebase already holds this rule, in decodeImage: "the
// format is detected from the magic bytes, never from a file extension: a
// server hands you a content type and a body, not a filename."
//
// "COMPRESSED" DOES NOT MEAN "EXTRACT IT". `.chd` and `.rvz` are compressed
// formats that cores read natively — `chd` is in genesis_plus_gx's own
// valid_extensions. Helpfully unpacking one produces something no core can
// load. What decides is not whether the payload is compressed but whether the
// CORE can read it as it stands.
//
// ASK THE CORE, NOT THE PLATFORM. `retro_get_system_info` reports both the
// extensions a core accepts and `block_extract`, which is a core saying "give
// me the archive, I read it myself". Both are per core and neither is knowable
// from the platform.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace romfile {

// What a payload actually is, from its leading bytes.
enum class Kind {
    Plain,      // a ROM, or a format we should not touch
    Zip,
    SevenZip,
    Rar,
    Tar,
    Gzip,
    // Called out so they are never mistaken for archives to unpack: both are
    // compressed containers that cores open themselves.
    Chd,
    Rvz,
};

Kind sniff(const std::vector<uint8_t>& bytes);
const char* kindName(Kind k);
bool isContainer(Kind k);   // something libarchive should open

struct Member {
    std::string name;
    std::vector<uint8_t> bytes;
};

// Unpacks every member. All of them, not just the interesting one: a fullpath
// core given a `.cue` needs its `.bin` sitting beside it, and a multi-disc game
// needs the discs its `.m3u` names.
bool extractAll(const std::vector<uint8_t>& in, std::vector<Member>* out,
                std::string* err);

// What a core should be given.
struct Prepared {
    // Hand over the downloaded bytes untouched. True for a plain ROM, for a
    // `.chd`, and for a core that asked not to have its archives opened.
    bool passThrough = false;
    std::vector<Member> members;
    // Index into `members` of the file the core should actually be pointed at.
    int primary = -1;
    Kind kind = Kind::Plain;
};

// `validExtensions` is the core's own, pipe-separated, as libretro reports it:
// "gb|gbc|dmg". `blockExtract` is the core's own flag.
bool prepare(const std::vector<uint8_t>& downloaded, const std::string& validExtensions,
             bool blockExtract, Prepared* out, std::string* err);

}  // namespace romfile
