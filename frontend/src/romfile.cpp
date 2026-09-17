#include "romfile.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstring>

namespace romfile {
namespace {

bool starts(const std::vector<uint8_t>& b, const char* magic, size_t n, size_t at = 0) {
    if (b.size() < at + n) return false;
    return std::memcmp(b.data() + at, magic, n) == 0;
}

// Lowercased text after the last dot, or empty. Used ONLY to match a member of
// an archive against a core's valid_extensions — never to decide what the
// downloaded payload is.
std::string extensionOf(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) return {};
    std::string e = name.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool coreAccepts(const std::string& validExtensions, const std::string& ext) {
    if (ext.empty() || validExtensions.empty()) return false;
    size_t start = 0;
    while (start <= validExtensions.size()) {
        const size_t bar = validExtensions.find('|', start);
        const size_t end = (bar == std::string::npos) ? validExtensions.size() : bar;
        if (validExtensions.compare(start, end - start, ext) == 0) return true;
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return false;
}

}  // namespace

Kind sniff(const std::vector<uint8_t>& b) {
    // Order matters only in that the native-container checks come first: being
    // wrong about those is the expensive mistake, because unpacking one
    // produces something no core can open.
    if (starts(b, "MComprHD", 8)) return Kind::Chd;
    if (starts(b, "RVZ\x01", 4)) return Kind::Rvz;

    // "PK\x03\x04" is a normal archive; the other two are an empty archive and
    // a spanned one, and neither is something to try to read a ROM out of.
    if (starts(b, "PK\x03\x04", 4)) return Kind::Zip;
    if (starts(b, "PK\x05\x06", 4) || starts(b, "PK\x07\x08", 4)) return Kind::Zip;
    if (starts(b, "7z\xBC\xAF\x27\x1C", 6)) return Kind::SevenZip;
    if (starts(b, "Rar!\x1A\x07", 6)) return Kind::Rar;
    if (starts(b, "\x1F\x8B", 2)) return Kind::Gzip;
    // tar keeps its magic 257 bytes in, which is why a tar cannot be
    // recognised from a short prefix.
    if (starts(b, "ustar", 5, 257)) return Kind::Tar;
    return Kind::Plain;
}

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Zip: return "zip";
        case Kind::SevenZip: return "7z";
        case Kind::Rar: return "rar";
        case Kind::Tar: return "tar";
        case Kind::Gzip: return "gzip";
        case Kind::Chd: return "chd";
        case Kind::Rvz: return "rvz";
        default: return "plain";
    }
}

bool isContainer(Kind k) {
    return k == Kind::Zip || k == Kind::SevenZip || k == Kind::Rar ||
           k == Kind::Tar || k == Kind::Gzip;
}

bool extractAll(const std::vector<uint8_t>& in, std::vector<Member>* out,
                std::string* err) {
    out->clear();
    struct archive* a = archive_read_new();
    if (!a) { if (err) *err = "archive_read_new failed"; return false; }
    // Everything libarchive knows, rather than a list of formats we guessed
    // people would have. The point of the library is not having that list.
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    if (archive_read_open_memory(a, const_cast<uint8_t*>(in.data()), in.size()) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_read_free(a);
        return false;
    }

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;   // skip directories
        const char* name = archive_entry_pathname(entry);
        Member m;
        m.name = name ? name : "";

        // la_int64 size can be -1 when the format does not record it up front,
        // so this reads until EOF rather than trusting it.
        for (;;) {
            char buf[64 * 1024];
            const ssize_t n = archive_read_data(a, buf, sizeof buf);
            if (n < 0) {
                if (err) *err = archive_error_string(a) ? archive_error_string(a)
                                                       : "read failed";
                archive_read_free(a);
                return false;
            }
            if (n == 0) break;
            m.bytes.insert(m.bytes.end(), buf, buf + n);
        }
        out->push_back(std::move(m));
    }

    archive_read_free(a);
    return true;
}

Kind sniffFile(const std::string& path, std::string* err) {
    // 512 is enough for every signature here, including tar's, which sits 257
    // bytes in. Reading more of a multi-gigabyte file to identify it would
    // defeat the point of not holding it.
    std::vector<uint8_t> head(512);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "cannot open " + path; return Kind::Plain; }
    const size_t n = std::fread(head.data(), 1, head.size(), f);
    std::fclose(f);
    head.resize(n);
    return sniff(head);
}

int64_t unpackedSize(const std::string& path, const std::string& validExtensions,
                     bool blockExtract) {
    // Nothing will be written, so nothing is needed. The same three conditions
    // prepareFile uses to hand a file over untouched.
    const Kind k = sniffFile(path, nullptr);
    if (blockExtract || !isContainer(k) || coreAccepts(validExtensions, kindName(k)))
        return 0;

    struct archive* a = archive_read_new();
    if (!a) return 0;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path.c_str(), 256 * 1024) != ARCHIVE_OK) {
        archive_read_free(a);
        return 0;
    }

    // Reading HEADERS only, never data — the size is in the archive's own index
    // and costs a seek rather than a decompression.
    int64_t total = 0;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        // A format that streams may not declare a size. Zero then means "I do
        // not know" rather than "empty", and the honest answer is to say so
        // rather than to invent a ratio: the caller extracts into whatever room
        // there is and fails if it runs out.
        if (!archive_entry_size_is_set(entry)) {
            archive_read_free(a);
            return 0;
        }
        total += archive_entry_size(entry);
    }
    archive_read_free(a);
    return total;
}

bool prepareFile(const std::string& downloadedPath, const std::string& outDir,
                 const std::string& validExtensions, bool blockExtract,
                 std::string* primaryPath, Kind* kindOut, std::string* err) {
    const Kind k = sniffFile(downloadedPath, err);
    if (kindOut) *kindOut = k;

    // Hand it over as it stands: the core asked to open its own archives, or
    // it is not a container, or it is one this core reads natively — a .chd
    // being the case that matters, since unpacking one produces something no
    // core can load.
    if (blockExtract || !isContainer(k) || coreAccepts(validExtensions, kindName(k))) {
        *primaryPath = downloadedPath;
        return true;
    }

    struct archive* a = archive_read_new();
    if (!a) { if (err) *err = "archive_read_new failed"; return false; }
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, downloadedPath.c_str(), 256 * 1024) != ARCHIVE_OK) {
        if (err) *err = archive_error_string(a) ? archive_error_string(a) : "cannot open";
        archive_read_free(a);
        return false;
    }

    std::string best;       // the member the core named
    std::string biggest;    // fallback, for a set whose members have no extension
    int64_t biggestSize = -1;
    bool wroteAny = false;

    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        const char* raw = archive_entry_pathname(entry);
        if (!raw) continue;
        std::string name = raw;

        // An archive is untrusted input. A member named "../../etc/thing" must
        // land inside outDir or nowhere: keep the last path component only.
        const size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (name.empty() || name == "." || name == "..") continue;

        const std::string out = outDir + "/" + name;
        FILE* f = std::fopen(out.c_str(), "wb");
        if (!f) { if (err) *err = "cannot write " + out; archive_read_free(a); return false; }

        int64_t written = 0;
        std::vector<char> buf(256 * 1024);
        for (;;) {
            const ssize_t got = archive_read_data(a, buf.data(), buf.size());
            if (got < 0) {
                std::fclose(f);
                if (err) *err = archive_error_string(a) ? archive_error_string(a) : "read failed";
                archive_read_free(a);
                return false;
            }
            if (got == 0) break;
            if (std::fwrite(buf.data(), 1, static_cast<size_t>(got), f) !=
                static_cast<size_t>(got)) {
                std::fclose(f);
                if (err) *err = "short write to " + out;
                archive_read_free(a);
                return false;
            }
            written += got;
        }
        std::fclose(f);
        wroteAny = true;

        if (best.empty() && coreAccepts(validExtensions, extensionOf(name))) best = out;
        if (written > biggestSize) { biggestSize = written; biggest = out; }
    }
    archive_read_free(a);

    if (!wroteAny) {
        if (err) *err = std::string("the ") + kindName(k) + " held no files";
        return false;
    }
    *primaryPath = best.empty() ? biggest : best;
    return true;
}

bool prepare(const std::vector<uint8_t>& downloaded, const std::string& validExtensions,
             bool blockExtract, Prepared* out, std::string* err) {
    out->members.clear();
    out->primary = -1;
    out->kind = sniff(downloaded);

    // The core asked for the archive itself. Some read their own, and a core
    // knows better than we do.
    if (blockExtract) { out->passThrough = true; return true; }

    // Not a container at all — a plain ROM, a .chd, a .rvz.
    if (!isContainer(out->kind)) { out->passThrough = true; return true; }

    // A container the core reads natively. genesis_plus_gx lists `chd`; some
    // cores list `zip`. Unpacking it would be actively wrong.
    if (coreAccepts(validExtensions, kindName(out->kind))) {
        out->passThrough = true;
        return true;
    }

    if (!extractAll(downloaded, &out->members, err)) return false;
    if (out->members.empty()) {
        if (err) *err = std::string("the ") + kindName(out->kind) + " held no files";
        return false;
    }

    // The member the core said it wants.
    for (size_t i = 0; i < out->members.size(); ++i) {
        if (coreAccepts(validExtensions, extensionOf(out->members[i].name))) {
            out->primary = static_cast<int>(i);
            break;
        }
    }
    // Nothing matched. Rather than fail, take the largest file: a set whose
    // members have no extension is a real thing — 32 of them in the reference
    // library — and the ROM is reliably the biggest thing in the archive
    // beside its own metadata. Reported by the caller, never silent.
    if (out->primary < 0) {
        size_t best = 0;
        for (size_t i = 1; i < out->members.size(); ++i)
            if (out->members[i].bytes.size() > out->members[best].bytes.size()) best = i;
        out->primary = static_cast<int>(best);
    }
    out->passThrough = false;
    return true;
}

}  // namespace romfile
