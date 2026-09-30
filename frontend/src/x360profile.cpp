#include "x360profile.h"

#include "storage.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace cab::x360profile {
namespace {

// XeKey 0x19, retail, as Xenia Edge has it (crypto_utils.cc, xekey_0x19).
constexpr uint8_t kKey[16] = {0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9,
                              0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3};

constexpr size_t kAccountInfo = 0x17C;           // sizeof(X_XAMACCOUNTINFO)
constexpr size_t kPlain = 8 + kAccountInfo;      // the confounder, then that
constexpr size_t kFile = 0x10 + kPlain;          // 404
constexpr size_t kGamertagAt = 8;                // after reserved and live flags

// SHA-1, FIPS 180-4. Small enough to keep here rather than link a library
// for one 404-byte file.
struct Sha1 {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<uint8_t> buf;
    uint64_t bits = 0;

    static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 |
                   uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    }
    void add(const uint8_t* p, size_t n) {
        bits += uint64_t(n) * 8;
        buf.insert(buf.end(), p, p + n);
        size_t at = 0;
        for (; at + 64 <= buf.size(); at += 64) block(buf.data() + at);
        buf.erase(buf.begin(), buf.begin() + static_cast<long>(at));
    }
    std::array<uint8_t, 20> done() {
        const uint64_t total = bits;
        const uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (buf.size() != 56) add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(total >> (56 - 8 * i));
        add(len, 8);
        std::array<uint8_t, 20> out{};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 4; ++j) out[i * 4 + j] = uint8_t(h[i] >> (24 - 8 * j));
        return out;
    }
};

std::array<uint8_t, 20> hmac(const uint8_t* data, size_t n) {
    uint8_t ipad[64], opad[64];
    std::memset(ipad, 0x36, 64);
    std::memset(opad, 0x5C, 64);
    for (size_t i = 0; i < sizeof kKey; ++i) ipad[i] ^= kKey[i], opad[i] ^= kKey[i];
    Sha1 inner;
    inner.add(ipad, 64);
    inner.add(data, n);
    const std::array<uint8_t, 20> in = inner.done();
    Sha1 outer;
    outer.add(opad, 64);
    outer.add(in.data(), in.size());
    return outer.done();
}

// RC4 with a 16-byte key, in place (crypto_utils.cc, RC4).
void rc4(const uint8_t* key, uint8_t* data, size_t n) {
    uint8_t s[256];
    for (int i = 0; i < 256; ++i) s[i] = uint8_t(i);
    for (int i = 0, j = 0; i < 256; ++i) {
        j = (j + s[i] + key[i % 16]) & 0xFF;
        std::swap(s[i], s[j]);
    }
    for (size_t k = 0, i = 0, j = 0; k < n; ++k) {
        i = (i + 1) & 0xFF;
        j = (j + s[i]) & 0xFF;
        std::swap(s[i], s[j]);
        data[k] ^= s[(s[i] + s[j]) & 0xFF];
    }
}

}  // namespace

std::string gamertag(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        // Punctuation separates words, as a space would ("mm_tech" reads
        // "mm tech"); anything outside ASCII is dropped, as the 360 had none.
        const bool separator = c > 0 && c < 0x7F && !alnum;
        if (alnum) out += c;
        else if (separator && !out.empty() && out.back() != ' ') out += ' ';
        if (out.size() == 15) break;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? "Player" : out;
}

std::string accountPath(const std::string& contentRoot) {
    return contentRoot + "/" + kXuid + "/FFFE07D1/00010000/" + kXuid + "/Account";
}

bool writeAccount(const std::string& path, const std::string& tag, std::string* err) {
    uint8_t file[kFile] = {};
    uint8_t* plain = file + 0x10;
    std::memset(plain, 0xFD, 8);   // the confounder, as Edge writes it
    // UTF-16, big-endian, at most 15 characters and a terminator: gamertag()
    // is ASCII, so each is one unit.
    uint8_t* at = plain + 8 + kGamertagAt;
    for (size_t i = 0; i < tag.size() && i < 15; ++i) at[i * 2 + 1] = uint8_t(tag[i]);
    const std::array<uint8_t, 20> hash = hmac(plain, kPlain);
    std::memcpy(file, hash.data(), 0x10);
    const std::array<uint8_t, 20> key = hmac(hash.data(), 0x10);
    rc4(key.data(), plain, kPlain);

    storage::makeDirs(path.substr(0, path.find_last_of('/')));
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file), sizeof file);
        if (!out) {
            *err = "could not write " + tmp;
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        *err = "could not put the profile at " + path;
        return false;
    }
    return true;
}

std::string readAccount(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() != kFile) return {};
    const std::array<uint8_t, 20> key = hmac(d.data(), 0x10);
    rc4(key.data(), d.data() + 0x10, kPlain);
    if (std::memcmp(hmac(d.data() + 0x10, kPlain).data(), d.data(), 0x10) != 0) return {};
    std::string tag;
    const uint8_t* at = d.data() + 0x10 + 8 + kGamertagAt;
    for (size_t i = 0; i < 16 && (at[i * 2] || at[i * 2 + 1]); ++i)
        tag += at[i * 2] ? '?' : char(at[i * 2 + 1]);
    return tag;
}

}  // namespace cab::x360profile
