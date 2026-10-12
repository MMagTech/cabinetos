#include "manual.h"

#include "proc.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace manual {

namespace {

// The paths build_files/build.sh asserts.
constexpr const char* kPdfInfo = "/usr/bin/pdfinfo";
constexpr const char* kPdfToPpm = "/usr/bin/pdftoppm";

// A whole page at twice the TV's size is 0.2 s on the A9; this is for a
// machine far slower than that, or a page that never finishes.
constexpr int kRenderTimeout = 30;

}  // namespace

std::vector<PageSize> pages(const std::string& pdf, std::string* err) {
    std::vector<PageSize> out;
    // Past the last page is taken as the last page, so one call lists them
    // all: "Page    3 size:  554 x 810 pts" and "Page    3 rot:   0".
    const proc::Result r =
        proc::run({kPdfInfo, "-f", "1", "-l", "1000000", pdf}, 20);
    if (!r.ok()) {
        if (err) *err = r.status < 0 ? "pdfinfo did not run" : proc::trimmed(r.err);
        return out;
    }
    size_t at = 0;
    while (at < r.out.size()) {
        size_t end = r.out.find('\n', at);
        if (end == std::string::npos) end = r.out.size();
        const std::string line = r.out.substr(at, end - at);
        at = end + 1;
        int n = 0;
        float w = 0, h = 0;
        int rot = 0;
        char what[8] = {};
        if (std::sscanf(line.c_str(), "Page %d %7[a-z]: %f x %f", &n, what, &w, &h) == 4 &&
            std::strcmp(what, "size") == 0 && n == static_cast<int>(out.size()) + 1) {
            out.push_back({w, h});
        } else if (std::sscanf(line.c_str(), "Page %d rot: %d", &n, &rot) == 2 &&
                   n == static_cast<int>(out.size()) && (rot % 180) != 0) {
            std::swap(out.back().w, out.back().h);
        }
    }
    if (out.empty() && err) *err = "no pages";
    return out;
}

bool render(const std::string& pdf, int page, int width, Picture* out, std::string* err,
            const std::atomic<bool>* cancel) {
    *out = Picture{};
    const std::string p = std::to_string(page);
    // No output name: the picture comes back on standard output as one PPM,
    // so nothing is written to the disk. -cropbox is the page as a reader
    // sees it, and the size pdfinfo reported.
    const proc::Result r = proc::run({kPdfToPpm, "-f", p, "-l", p, "-cropbox", "-scale-to-x",
                                      std::to_string(width), "-scale-to-y", "-1", pdf},
                                     kRenderTimeout, cancel);
    if (!r.ok()) {
        if (err)
            *err = r.timedOut ? "pdftoppm took too long"
                              : r.status < 0 ? "pdftoppm did not run" : proc::trimmed(r.err);
        return false;
    }
    // "P6", the width, the height and 255, each after whitespace; then the
    // pixels, after exactly one more.
    const std::string& s = r.out;
    size_t i = 0;
    auto number = [&](int* v) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        const size_t start = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i == start) return false;
        *v = std::atoi(s.substr(start, i - start).c_str());
        return true;
    };
    int w = 0, h = 0, maxv = 0;
    if (s.compare(0, 2, "P6") != 0) {
        if (err) *err = "pdftoppm's picture was not a PPM";
        return false;
    }
    i = 2;
    if (!number(&w) || !number(&h) || !number(&maxv) || maxv != 255 || w <= 0 || h <= 0) {
        if (err) *err = "pdftoppm's picture had no size";
        return false;
    }
    ++i;
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
    if (s.size() - i < bytes) {
        if (err) *err = "pdftoppm's picture was cut short";
        return false;
    }
    out->width = w;
    out->height = h;
    out->rgb.assign(s.begin() + static_cast<std::ptrdiff_t>(i),
                    s.begin() + static_cast<std::ptrdiff_t>(i + bytes));
    return true;
}

bool looksLikePdf(const std::string& path) {
    char head[5] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const size_t n = std::fread(head, 1, 5, f);
    std::fclose(f);
    return n == 5 && std::memcmp(head, "%PDF-", 5) == 0;
}

}  // namespace manual
