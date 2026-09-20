#include "qr.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace qr {
namespace {

// --- The tables, all of them from ISO/IEC 18004 -----------------------------
//
// Error correction level M only. Each row is one version: how many codewords
// the symbol holds in total, how many error correction codewords each block
// carries, and the block layout — the spec splits data into one or two groups
// whose blocks differ in length by exactly one codeword.
//
// THE CHECK THAT THESE ARE RIGHT is arithmetic and it is asserted at the bottom
// of this block: g1Blocks*g1Data + g2Blocks*g2Data + blocks*ecPerBlock must
// equal totalCodewords, for every row. A transcription slip in a table like
// this produces a symbol that is structurally perfect and scans as nothing.
struct VersionSpec {
    int totalCodewords;
    int ecPerBlock;
    int g1Blocks, g1Data;
    int g2Blocks, g2Data;
};

constexpr int kMinVersion = 1;
constexpr int kMaxVersion = 10;

// Index by version; [0] is unused so the numbers read as the spec writes them.
constexpr VersionSpec kVersions[kMaxVersion + 1] = {
    {0, 0, 0, 0, 0, 0},
    { 26, 10, 1, 16, 0,  0},   // 1-M
    { 44, 16, 1, 28, 0,  0},   // 2-M
    { 70, 26, 1, 44, 0,  0},   // 3-M
    {100, 18, 2, 32, 0,  0},   // 4-M
    {134, 24, 2, 43, 0,  0},   // 5-M
    {172, 16, 4, 27, 0,  0},   // 6-M
    {196, 18, 4, 31, 0,  0},   // 7-M
    {242, 22, 2, 38, 2, 39},   // 8-M
    {292, 22, 3, 36, 2, 37},   // 9-M
    {346, 26, 4, 43, 1, 44},   // 10-M
};

// Alignment pattern centre coordinates, per version. Version 1 has none.
constexpr int kAlignCount[kMaxVersion + 1] = {0, 0, 2, 2, 2, 2, 2, 3, 3, 3, 3};
constexpr int kAlign[kMaxVersion + 1][3] = {
    {0, 0, 0},
    {0, 0, 0},   // 1 — none
    {6, 18, 0},  // 2
    {6, 22, 0},  // 3
    {6, 26, 0},  // 4
    {6, 30, 0},  // 5
    {6, 34, 0},  // 6
    {6, 22, 38}, // 7
    {6, 24, 42}, // 8
    {6, 26, 46}, // 9
    {6, 28, 50}, // 10
};

int dataCodewords(int v) {
    const VersionSpec& s = kVersions[v];
    return s.g1Blocks * s.g1Data + s.g2Blocks * s.g2Data;
}
int blockCount(int v) { return kVersions[v].g1Blocks + kVersions[v].g2Blocks; }
int sizeOf(int v) { return 17 + 4 * v; }

// The character count field is 8 bits in byte mode up to version 9 and 16 bits
// from version 10. That boundary sits inside the range this file supports,
// which is exactly why it is a function and not a constant.
int countBits(int v) { return v < 10 ? 8 : 16; }

bool tablesConsistent() {
    for (int v = kMinVersion; v <= kMaxVersion; ++v) {
        const VersionSpec& s = kVersions[v];
        if (dataCodewords(v) + blockCount(v) * s.ecPerBlock != s.totalCodewords)
            return false;
    }
    return true;
}

// --- GF(256), with the QR primitive polynomial 0x11D ------------------------

struct Gf {
    uint8_t exp[512];
    uint8_t log[256];
    Gf() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = static_cast<uint8_t>(x);
            log[x] = static_cast<uint8_t>(i);
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        // Doubled, so a product of two logs can be looked up without a modulo.
        for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
        log[0] = 0;   // never read; multiplication special-cases zero
    }
    uint8_t mul(uint8_t a, uint8_t b) const {
        if (a == 0 || b == 0) return 0;
        return exp[log[a] + log[b]];
    }
};

const Gf& gf() {
    static const Gf g;
    return g;
}

// The generator polynomial for `degree` error correction codewords:
//   (x - a^0)(x - a^1) ... (x - a^(degree-1))
std::vector<uint8_t> generatorPoly(int degree) {
    std::vector<uint8_t> poly{1};
    for (int i = 0; i < degree; ++i) {
        std::vector<uint8_t> next(poly.size() + 1, 0);
        for (size_t j = 0; j < poly.size(); ++j) {
            next[j] ^= poly[j];
            next[j + 1] ^= gf().mul(poly[j], gf().exp[i]);
        }
        poly = std::move(next);
    }
    return poly;
}

// Polynomial long division; the remainder IS the error correction block.
std::vector<uint8_t> errorCorrection(const std::vector<uint8_t>& data, int ecLen) {
    const std::vector<uint8_t> gen = generatorPoly(ecLen);
    std::vector<uint8_t> rem(static_cast<size_t>(ecLen), 0);
    for (uint8_t d : data) {
        const uint8_t factor = d ^ rem[0];
        rem.erase(rem.begin());
        rem.push_back(0);
        if (factor != 0) {
            for (size_t i = 0; i < gen.size() - 1; ++i)
                rem[i] ^= gf().mul(gen[i + 1], factor);
        }
    }
    return rem;
}

// --- BCH, for the format and version information ----------------------------

// BCH(15,5) over the 5-bit format value, then XOR'd with the spec's mask so a
// format of all zeros is not all zeros on the symbol.
uint32_t formatBits(int maskPattern) {
    // Error correction level M is `00` in the two high bits.
    const uint32_t value = static_cast<uint32_t>(0 << 3 | maskPattern);
    // The remainder of value<<10 divided by the generator, built up one shift
    // at a time. THE BIT TESTED IS THE GENERATOR'S DEGREE — 10 — and not the
    // width of the finished field. Testing bit 14 instead reduces nothing,
    // because the running remainder never gets that wide, and the result is a
    // format field with no error correction in it at all. That produced a
    // symbol whose data region was perfect and which no scanner would read.
    uint32_t rem = value;
    for (int i = 0; i < 10; ++i) {
        rem <<= 1;
        if (rem & (1u << 10)) rem ^= 0x537;
    }
    return ((value << 10) | (rem & 0x3FF)) ^ 0x5412;
}

// BCH(18,6). Only versions 7 and above carry this.
uint32_t versionBits(int version) {
    // Degree 12, so bit 12 is the one to test. Same trap as formatBits above.
    uint32_t rem = static_cast<uint32_t>(version);
    for (int i = 0; i < 12; ++i) {
        rem <<= 1;
        if (rem & (1u << 12)) rem ^= 0x1F25;
    }
    return (static_cast<uint32_t>(version) << 12) | (rem & 0xFFF);
}

// --- The symbol -------------------------------------------------------------

// Two grids of the same size: the modules, and which of them are function
// patterns. The second is not an optimisation — masking must touch the data
// region and nothing else, and "is this a function module" is not a question
// the finished picture can answer.
struct Canvas {
    int size = 0;
    std::vector<uint8_t> mod;
    std::vector<uint8_t> fixed;

    explicit Canvas(int n)
        : size(n), mod(static_cast<size_t>(n) * n, 0),
          fixed(static_cast<size_t>(n) * n, 0) {}

    void set(int x, int y, bool dark, bool isFunction) {
        if (x < 0 || y < 0 || x >= size || y >= size) return;
        mod[static_cast<size_t>(y) * size + x] = dark ? 1 : 0;
        if (isFunction) fixed[static_cast<size_t>(y) * size + x] = 1;
    }
    bool dark(int x, int y) const {
        return mod[static_cast<size_t>(y) * size + x] != 0;
    }
    bool isFixed(int x, int y) const {
        return fixed[static_cast<size_t>(y) * size + x] != 0;
    }
};

void drawFinder(Canvas& c, int cx, int cy) {
    // The 7x7 eye plus its one-module separator, drawn as one loop over the
    // 9x9 that contains both. Anything falling outside the symbol is dropped by
    // `set`, which is what makes the three corners share this code.
    for (int dy = -1; dy <= 7; ++dy) {
        for (int dx = -1; dx <= 7; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= c.size || y >= c.size) continue;
            const int r = std::max(std::abs(dx - 3), std::abs(dy - 3));
            // Rings at distance 2 and 4 are light; the centre and the outer
            // ring are dark. The separator is the r > 3 case.
            c.set(x, y, r != 2 && r <= 3, true);
        }
    }
}

void drawAlignment(Canvas& c, int cx, int cy) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            c.set(cx + dx, cy + dy,
                  std::max(std::abs(dx), std::abs(dy)) != 1, true);
}

void drawFunctionPatterns(Canvas& c, int version) {
    const int n = c.size;

    drawFinder(c, 0, 0);
    drawFinder(c, n - 7, 0);
    drawFinder(c, 0, n - 7);

    // Timing: alternating modules along row 6 and column 6, between the eyes.
    for (int i = 8; i < n - 8; ++i) {
        c.set(i, 6, i % 2 == 0, true);
        c.set(6, i, i % 2 == 0, true);
    }

    // Alignment patterns at every pairing of the version's centres, except the
    // three that would sit on top of a finder.
    const int count = kAlignCount[version];
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < count; ++j) {
            const bool onFinder = (i == 0 && j == 0) ||
                                  (i == 0 && j == count - 1) ||
                                  (i == count - 1 && j == 0);
            if (onFinder) continue;
            drawAlignment(c, kAlign[version][j], kAlign[version][i]);
        }
    }

    // The format information areas are reserved now and written once the mask
    // is chosen. Reserving them matters: the data placement walk must step over
    // them, and it decides that by asking whether a module is a function one.
    // i == 6 IS SKIPPED AND MUST STAY SKIPPED. (6,8) and (8,6) look like they
    // belong to the format area because they sit inside its L shape, but they
    // are the timing pattern crossing it, and the timing loop above has already
    // drawn them. Reserving them blanks two modules of the timing line, which
    // is a symbol every scanner rejects for a reason nothing in the data
    // explains.
    for (int i = 0; i < 9; ++i) {
        if (i != 6) {
            c.set(i, 8, false, true);
            c.set(8, i, false, true);
        }
    }
    for (int i = 0; i < 8; ++i) {
        c.set(n - 1 - i, 8, false, true);
        c.set(8, n - 1 - i, false, true);
    }
    // The module that is always dark. Not decoration — a decoder uses it.
    c.set(8, n - 8, true, true);

    if (version >= 7) {
        const uint32_t bits = versionBits(version);
        for (int i = 0; i < 18; ++i) {
            const bool on = ((bits >> i) & 1) != 0;
            const int a = i / 3, b = i % 3;
            c.set(a, n - 11 + b, on, true);
            c.set(n - 11 + b, a, on, true);
        }
    }
}

void drawFormat(Canvas& c, int maskPattern) {
    const int n = c.size;
    const uint32_t bits = formatBits(maskPattern);
    for (int i = 0; i < 15; ++i) {
        const bool on = ((bits >> i) & 1) != 0;
        // The first copy, around the top-left eye. Column 6 and row 6 are
        // timing and are stepped over, which is what the two offsets encode.
        if (i < 6)       c.set(8, i, on, true);
        else if (i == 6) c.set(8, 7, on, true);
        else if (i == 7) c.set(8, 8, on, true);
        else if (i == 8) c.set(7, 8, on, true);
        else             c.set(14 - i, 8, on, true);

        // The second copy, split between the other two eyes, so the format
        // survives losing a corner.
        if (i < 8) c.set(n - 1 - i, 8, on, true);
        else       c.set(8, n - 15 + i, on, true);
    }
}

// The zigzag: two columns at a time from the right, upward then downward,
// skipping the vertical timing column entirely.
void placeData(Canvas& c, const std::vector<uint8_t>& stream) {
    const int n = c.size;
    size_t bit = 0;
    const size_t totalBits = stream.size() * 8;
    bool upward = true;

    for (int right = n - 1; right >= 1; right -= 2) {
        // Column 6 is timing all the way down. The spec does not narrow the
        // pair around it — it removes the column from the walk, so every pair
        // to its left shifts by one.
        if (right == 6) right = 5;
        for (int v = 0; v < n; ++v) {
            const int y = upward ? n - 1 - v : v;
            for (int k = 0; k < 2; ++k) {
                const int x = right - k;
                if (c.isFixed(x, y)) continue;
                // REMAINDER BITS ARE LIGHT, NOT ABSENT. Several versions end
                // with a few modules the data does not reach; they are placed
                // as zeros and then masked like any other data module. Leaving
                // them out of the walk shifts everything after them.
                bool on = false;
                if (bit < totalBits) {
                    on = ((stream[bit >> 3] >> (7 - (bit & 7))) & 1) != 0;
                    ++bit;
                }
                c.set(x, y, on, false);
            }
        }
        upward = !upward;
    }
}

bool maskAt(int pattern, int x, int y) {
    switch (pattern) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (y / 2 + x / 3) % 2 == 0;
        case 5: return (x * y) % 2 + (x * y) % 3 == 0;
        case 6: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
        default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
    }
}

void applyMask(Canvas& c, int pattern) {
    for (int y = 0; y < c.size; ++y)
        for (int x = 0; x < c.size; ++x)
            if (!c.isFixed(x, y) && maskAt(pattern, x, y))
                c.mod[static_cast<size_t>(y) * c.size + x] ^= 1;
}

// The four penalty rules. Lower is better; this is how the mask is chosen, and
// it is the reason a QR encoder cannot simply pick one and stop — a bad mask
// produces large blocks of one colour and patterns a decoder mistakes for
// finders.
int penalty(const Canvas& c) {
    const int n = c.size;
    int score = 0;

    // Rule 1: runs of five or more of the same colour, in both directions.
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < n; ++a) {
            int runLen = 1;
            bool runDark = pass == 0 ? c.dark(0, a) : c.dark(a, 0);
            for (int b = 1; b < n; ++b) {
                const bool d = pass == 0 ? c.dark(b, a) : c.dark(a, b);
                if (d == runDark) {
                    ++runLen;
                } else {
                    if (runLen >= 5) score += 3 + (runLen - 5);
                    runDark = d;
                    runLen = 1;
                }
            }
            if (runLen >= 5) score += 3 + (runLen - 5);
        }
    }

    // Rule 2: every 2x2 block of one colour.
    for (int y = 0; y < n - 1; ++y) {
        for (int x = 0; x < n - 1; ++x) {
            const bool d = c.dark(x, y);
            if (d == c.dark(x + 1, y) && d == c.dark(x, y + 1) &&
                d == c.dark(x + 1, y + 1))
                score += 3;
        }
    }

    // Rule 3: the finder-like sequence 1011101 with four light modules on ONE
    // side of it — eleven modules in all. This is the rule that stops a decoder
    // finding an eye in the middle of the data.
    //
    // THE WINDOW IS ELEVEN MODULES AND IT MUST FIT INSIDE THE ROW. The obvious
    // shortcut — look for the seven-module core, then treat anything past the
    // edge as light — scores every real finder pattern 40 twice over and adds
    // several hundred points of noise that swamps the differences between
    // masks. It chose a different mask from every other encoder, which is a
    // valid symbol produced by a wrong calculation and would never have shown
    // up in a scan test.
    static const bool kFinderish[11] = {true,  false, true,  true, true, false,
                                        true,  false, false, false, false};
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < n; ++a) {
            for (int b = 0; b + 11 <= n; ++b) {
                bool forward = true, backward = true;
                for (int k = 0; k < 11 && (forward || backward); ++k) {
                    const bool d = pass == 0 ? c.dark(b + k, a) : c.dark(a, b + k);
                    if (d != kFinderish[k]) forward = false;
                    if (d != kFinderish[10 - k]) backward = false;
                }
                if (forward || backward) score += 40;
            }
        }
    }

    // Rule 4: how far the proportion of dark modules is from half, in steps of
    // five percent.
    //
    // Written without floating point and without truncating first, because
    // rounding the percentage before measuring its distance from fifty moves
    // the answer by a whole step near the boundaries.
    int dark = 0;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (c.dark(x, y)) ++dark;
    const int total = n * n;
    const int steps = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    score += steps * 10;
    return score;
}

// Mode indicator, character count, the bytes, the terminator, and the pad
// pattern the spec names: 0xEC and 0x11, alternating, forever.
std::vector<uint8_t> buildStream(const std::string& text, int version) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int accBits = 0;
    auto push = [&](uint32_t value, int bits) {
        for (int i = bits - 1; i >= 0; --i) {
            acc = (acc << 1) | ((value >> i) & 1);
            if (++accBits == 8) {
                out.push_back(static_cast<uint8_t>(acc & 0xFF));
                acc = 0;
                accBits = 0;
            }
        }
    };

    push(0b0100, 4);                                       // byte mode
    push(static_cast<uint32_t>(text.size()), countBits(version));
    for (unsigned char ch : text) push(ch, 8);

    const int capacityBits = dataCodewords(version) * 8;
    const int used = static_cast<int>(out.size()) * 8 + accBits;
    push(0, std::min(4, capacityBits - used));             // terminator
    if (accBits > 0) push(0, 8 - accBits);                 // to a byte boundary

    bool useEc = true;
    while (static_cast<int>(out.size()) < dataCodewords(version)) {
        out.push_back(useEc ? 0xEC : 0x11);
        useEc = !useEc;
    }
    return out;
}

// Split into blocks, compute each block's error correction, then interleave —
// data codeword i of every block in turn, then EC codeword i of every block.
//
// THE SHORT BLOCKS HAVE NO CODEWORD AT THE LAST DATA INDEX and are skipped
// there rather than padded. Getting that wrong shifts every byte after it,
// which produces a symbol that scans and decodes to rubbish — the failure that
// looks least like a failure.
std::vector<uint8_t> interleave(const std::vector<uint8_t>& data, int version) {
    const VersionSpec& s = kVersions[version];
    const int blocks = blockCount(version);

    std::vector<std::vector<uint8_t>> dataBlocks, ecBlocks;
    size_t at = 0;
    for (int b = 0; b < blocks; ++b) {
        const int len = b < s.g1Blocks ? s.g1Data : s.g2Data;
        std::vector<uint8_t> block(data.begin() + static_cast<long>(at),
                                   data.begin() + static_cast<long>(at) + len);
        at += static_cast<size_t>(len);
        ecBlocks.push_back(errorCorrection(block, s.ecPerBlock));
        dataBlocks.push_back(std::move(block));
    }

    std::vector<uint8_t> out;
    out.reserve(static_cast<size_t>(s.totalCodewords));
    const int longest = std::max(s.g1Data, s.g2Blocks > 0 ? s.g2Data : 0);
    for (int i = 0; i < longest; ++i)
        for (const std::vector<uint8_t>& b : dataBlocks)
            if (i < static_cast<int>(b.size())) out.push_back(b[static_cast<size_t>(i)]);
    for (int i = 0; i < s.ecPerBlock; ++i)
        for (const std::vector<uint8_t>& b : ecBlocks)
            out.push_back(b[static_cast<size_t>(i)]);
    return out;
}

}  // namespace

Code encode(const std::string& text, std::string* err) {
    Code bad;
    if (!tablesConsistent()) {
        if (err) *err = "the QR version tables do not add up — see qr.cpp";
        return bad;
    }
    if (text.empty()) {
        if (err) *err = "nothing to encode";
        return bad;
    }

    int version = 0;
    for (int v = kMinVersion; v <= kMaxVersion; ++v) {
        const int need = 4 + countBits(v) + 8 * static_cast<int>(text.size());
        if (need <= dataCodewords(v) * 8) { version = v; break; }
    }
    if (version == 0) {
        if (err) {
            char buf[160];
            std::snprintf(buf, sizeof buf,
                          "%zu bytes will not fit in a version-10 QR code "
                          "(%d is the most this encoder holds)",
                          text.size(), dataCodewords(kMaxVersion) -
                                           (4 + countBits(kMaxVersion)) / 8 - 1);
            *err = buf;
        }
        return bad;
    }

    const std::vector<uint8_t> stream =
        interleave(buildStream(text, version), version);

    // Every mask is drawn in full and scored, and the lowest wins. There is no
    // shortcut: the penalty depends on the finished symbol, function patterns
    // included.
    int bestScore = -1;
    Canvas best(sizeOf(version));
    for (int mask = 0; mask < 8; ++mask) {
        Canvas c(sizeOf(version));
        drawFunctionPatterns(c, version);
        placeData(c, stream);
        applyMask(c, mask);
        drawFormat(c, mask);
        const int score = penalty(c);
        if (bestScore < 0 || score < bestScore) {
            bestScore = score;
            best = c;
        }
    }
    Code out;
    out.size = best.size;
    out.modules = best.mod;
    return out;
}

std::string toText(const Code& c, int quietZone) {
    if (!c.valid()) return {};
    // Two characters per module, because a terminal cell is about half as wide
    // as it is tall and a code drawn one-for-one is squashed into something no
    // scanner will read.
    //
    // DARK MODULES ARE DRAWN WITH SPACES AND LIGHT ONES WITH BLOCKS, which
    // looks inverted and is not. A terminal is light text on a dark background,
    // so the "ink" on screen is the background; printing blocks where the code
    // is dark produces a photographic negative, and a negative does not scan.
    std::string s;
    const int n = c.size + quietZone * 2;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const bool dark = c.at(x - quietZone, y - quietZone);
            s += dark ? "  " : "██";
        }
        s += '\n';
    }
    return s;
}

bool writePbm(const Code& c, const std::string& path, int scale, int quietZone) {
    if (!c.valid() || scale < 1) return false;
    const int n = (c.size + quietZone * 2) * scale;

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P4\n%d %d\n", n, n);

    // P4 packs eight pixels per byte, most significant bit leftmost, and each
    // ROW starts on a fresh byte. A 1 bit is black, which is the opposite of
    // every other format here and is the thing to get wrong.
    const int rowBytes = (n + 7) / 8;
    std::vector<uint8_t> row(static_cast<size_t>(rowBytes));
    bool ok = true;
    for (int y = 0; y < n && ok; ++y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < n; ++x) {
            const bool dark = c.at(x / scale - quietZone, y / scale - quietZone);
            if (dark) row[static_cast<size_t>(x / 8)] |= static_cast<uint8_t>(0x80 >> (x % 8));
        }
        ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    std::fclose(f);
    return ok;
}

}  // namespace qr
