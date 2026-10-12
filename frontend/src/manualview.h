// A GAME'S MANUAL ON THE TELEVISION (#364), beside its page.
//
// Decided with MMagTech on #364, 2026-10-12: R1 on a game's page slides the
// page aside and the manual in; L1 or B slides it back. One PDF page per
// screen, fitted to the TV, Left and Right to turn; A zooms one step for small
// print, and zoomed, the d-pad moves round the page (Left and Right turn it at
// its edge). Not in the in-game menu, and the last page is not remembered
// between visits: the manual keeps its place only while its page is open.
//
// One page per screen rather than pairs, because about 40% of the manuals
// RomM's scraper finds are already scanned as two-page spreads (the audit on
// #364).
//
// THE PAGES ARE PICTURES MADE WHEN IT OPENS (manual.h): the page on screen
// first, then the next and the one before, on a worker of its own, and
// uploaded here on the frame thread. Pages further away are let go. Zoomed,
// the page is drawn again at twice the size, and the fitted one is shown
// enlarged until it arrives (0.2 s on the A9).
//
// The app fetches the file and owns the slide; this owns the pages.

#pragma once

#include <GLES3/gl3.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "manual.h"
#include "screens.h"

namespace screens {

class ManualView {
public:
    ~ManualView();

    // A new manual, on its first page. `pxPerPoint` is the renderer's scale,
    // so pages are drawn at the TV's own resolution. Nothing shows until
    // setFile gives it the PDF.
    void open(int romId, float pxPerPoint);
    // The PDF is on the disk.
    void setFile(const std::string& pdf);
    // It could not be fetched, or it is not a PDF.
    void setFailed();
    // Lets every page go. Safe to call when it is not open.
    void close();
    bool isOpen() const { return romId_ != 0; }
    // Nothing still arriving or moving: the page on screen at full strength,
    // or the refusal. For a capture.
    bool settled() const;
    int romId() const { return romId_; }

    // Left, Right, Up, Down and A. Back is the app's: it slides the manual
    // away. Plays its own sounds.
    void key(Nav n);
    // Frame thread: advances the animations and uploads finished pages.
    void tick(float dt);
    // `t` is the slide, 0 with the game's page in front and 1 with the manual.
    void draw(Ctx& c, float t);
    void drawGlass(Ctx& c, float t);

private:
    enum class Status { Waiting, Ready, Failed };

    struct Texture {
        GLuint id = 0;
        int width = 0, height = 0;
        float fade = 0.0f;
    };
    // A page at the fitted size (level 1) or twice it (level 2).
    using Key = std::pair<int, int>;   // page from 0, level

    void wantPages();           // asks the worker for what is needed now
    void worker();
    void freeTexture(Texture& t);
    // The page's fitted rectangle, in design points, before zoom.
    void fitted(int page, float* w, float* h) const;
    // Moves the view to the page's point (x, y), 0..1 across and down, kept
    // on the page: centred on any side that fits the screen.
    void panTo(float x, float y, bool animate);
    void turn(int dir);
    void showCount();

    int romId_ = 0;
    float pxPerPoint_ = 1.0f;
    GLint maxTexture_ = 0;
    Status status_ = Status::Waiting;
    std::string pdf_;
    std::vector<manual::PageSize> pages_;
    int page_ = 0;
    std::map<Key, Texture> textures_;
    float waited_ = 0.0f;

    // The turn: the page left behind slides out as the new one comes in.
    int fromPage_ = -1;
    int turnDir_ = 0;
    design::Animated turn_;
    // Zoom: 1 fitted, 2 zoomed; the pan is the page's point (0..1 across and
    // down) held at the centre of the screen.
    bool zoomed_ = false;
    design::Animated zoom_;
    design::Animated panX_, panY_;
    // "Page 3 of 18", shown on arrival and after each turn, then let go.
    float countFor_ = 0.0f;
    design::Animated count_;

    // The worker. Requests are replaced wholesale each time the wanted set
    // changes; it takes the first one it has not done.
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::atomic<bool> cancel_{false};
    std::string workPdf_;
    bool wantSizes_ = false;
    struct Request {
        int page = 0, level = 1, width = 0;
    };
    std::deque<Request> requests_;
    struct Done {
        int page = 0, level = 1;
        manual::Picture picture;
        bool ok = false;
    };
    std::deque<Done> done_;
    bool sizesReady_ = false;
    std::vector<manual::PageSize> sizes_;
    std::string sizesError_;
};

}  // namespace screens
