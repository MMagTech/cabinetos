// A GAME'S MANUAL, ITS PAGES DRAWN BY THE BASE IMAGE'S POPPLER (#364).
//
// No PDF library in the frontend. The Bazzite base already carries
// poppler-utils, so the console runs `pdfinfo` once to learn the pages' sizes
// and `pdftoppm` for each page it shows, as it runs nmcli and wpctl.
// build_files/build.sh asserts both, because nothing else would notice them gone.
//
// MEASURED ON THE A9, 2026-10-12: one page at the TV's size takes 0.02 to
// 0.08 s, twice that size 0.12 to 0.2 s; a whole manual is up to 8 s and
// 77 MB of pictures (Sonic Adventure, 136 pages). So pages are drawn when
// the manual opens, the one on screen and its neighbours, and nothing but
// the PDF is ever kept on the disk: the pictures arrive on the tool's
// standard output and never touch a file.
//
// Both block, and are run on a worker (manualview.cpp).

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace manual {

// One page's size in points, its rotation already applied, so width over
// height is the shape it is shown in.
struct PageSize {
    float w = 0, h = 0;
};

// Every page's size, in order. Empty when the file is not a PDF poppler can
// open (damaged, locked); `err` says why.
std::vector<PageSize> pages(const std::string& pdf, std::string* err);

// Page `page` (from 1) drawn `width` pixels wide, its height following its
// shape. RGB, three bytes a pixel, rows top first.
struct Picture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgb;
};
// `cancel`, once set, stops it early: the manual was closed.
bool render(const std::string& pdf, int page, int width, Picture* out, std::string* err,
            const std::atomic<bool>* cancel = nullptr);

// Whether the file starts as a PDF does. A server that answers with a web page
// (a login, an error) must not be kept as somebody's manual.
bool looksLikePdf(const std::string& path);

}  // namespace manual

namespace romm { class Client; }

// A game's manual downloaded off RomM into `dest`, on main.cpp's manual
// worker. Moved out of main.cpp in the restructure (#398).
bool fetchManualFile(const romm::Client& client, const std::string& manualPath,
                     const std::string& dest, const std::function<bool()>& keepGoing,
                     std::string* err);
