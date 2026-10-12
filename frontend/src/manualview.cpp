#include "manualview.h"

#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screens {

namespace {

// Starting values, to be judged on the television.
// Room round a fitted page, so a TV that overscans keeps its edges.
constexpr float kMargin = 24.0f;
// The one zoom step.
constexpr float kZoom = 2.0f;
constexpr float kZoomTime = 0.25f;
// A press zoomed moves this much of the screen.
constexpr float kPanStep = 0.45f;
constexpr float kPanTime = 0.20f;
// A turn: the old page slides this far out as the new one slides in.
constexpr float kTurnTime = 0.22f;
constexpr float kTurnShift = 80.0f;
// Darker behind the manual than behind the game's page, over the same art.
constexpr float kScrim = 0.35f;
// "3 of 18": how long it stays after a turn, and its fade.
constexpr float kCountHold = 1.6f;
constexpr float kCountFade = 0.30f;
// A page arriving fades in, as art arriving over a network does.
constexpr float kPageFade = 0.20f;
// Said only when the first page is this slow.
constexpr float kSayWaiting = 0.6f;

}  // namespace

ManualView::~ManualView() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
        cancel_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    for (auto& [k, t] : textures_) freeTexture(t);
}

void ManualView::open(int romId, float pxPerPoint) {
    close();
    romId_ = romId;
    pxPerPoint_ = pxPerPoint > 0.0f ? pxPerPoint : 1.0f;
    if (!maxTexture_) glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture_);
    status_ = Status::Waiting;
    page_ = 0;
    fromPage_ = -1;
    zoomed_ = false;
    zoom_.settle(1.0f);
    panX_.settle(0.5f);
    panY_.settle(0.5f);
    turn_.settle(1.0f);
    count_.settle(0.0f);
    countFor_ = 0.0f;
    waited_ = 0.0f;
    if (!thread_.joinable()) thread_ = std::thread([this] { worker(); });
}

void ManualView::setFile(const std::string& pdf) {
    if (!isOpen()) return;
    pdf_ = pdf;
    std::lock_guard<std::mutex> lk(m_);
    workPdf_ = pdf;
    wantSizes_ = true;
    sizesReady_ = false;
    cv_.notify_all();
}

void ManualView::setFailed() {
    if (isOpen()) status_ = Status::Failed;
}

void ManualView::close() {
    {
        std::lock_guard<std::mutex> lk(m_);
        // A page being drawn for this manual stops, and anything it finishes
        // is thrown away: the worker checks the file it drew is still wanted.
        cancel_ = true;
        requests_.clear();
        done_.clear();
        wantSizes_ = false;
        sizesReady_ = false;
        workPdf_.clear();
    }
    for (auto& [k, t] : textures_) freeTexture(t);
    textures_.clear();
    pages_.clear();
    pdf_.clear();
    romId_ = 0;
}

bool ManualView::settled() const {
    if (status_ == Status::Failed) return true;
    if (status_ != Status::Ready) return false;
    auto it = textures_.find({page_, zoomed_ ? 2 : 1});
    return it != textures_.end() && it->second.fade >= 1.0f && turn_.value() >= 1.0f &&
           zoom_.elapsed >= zoom_.duration && panX_.elapsed >= panX_.duration &&
           panY_.elapsed >= panY_.duration;
}

void ManualView::freeTexture(Texture& t) {
    if (t.id) glDeleteTextures(1, &t.id);
    t.id = 0;
}

void ManualView::worker() {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
        cv_.wait(lk, [&] { return stop_ || wantSizes_ || !requests_.empty(); });
        if (stop_) return;
        const std::string pdf = workPdf_;
        if (wantSizes_) {
            wantSizes_ = false;
            lk.unlock();
            std::string err;
            std::vector<manual::PageSize> sizes = manual::pages(pdf, &err);
            lk.lock();
            if (workPdf_ == pdf) {
                sizes_ = std::move(sizes);
                sizesError_ = err;
                sizesReady_ = true;
            }
            continue;
        }
        const Request r = requests_.front();
        requests_.pop_front();
        cancel_ = false;
        lk.unlock();
        Done d;
        d.page = r.page;
        d.level = r.level;
        std::string err;
        d.ok = manual::render(pdf, r.page + 1, r.width, &d.picture, &err, &cancel_);
        if (!d.ok && !cancel_.load())
            std::fprintf(stderr, "[manual] page %d: %s\n", r.page + 1, err.c_str());
        lk.lock();
        if (workPdf_ == pdf && !cancel_.load()) done_.push_back(std::move(d));
    }
}

void ManualView::fitted(int page, float* w, float* h) const {
    const float boxW = ui::kCanvasWidth - 2.0f * kMargin;
    const float boxH = ui::kCanvasHeight - 2.0f * kMargin;
    float pw = 1.0f, ph = 1.0f;
    if (page >= 0 && page < static_cast<int>(pages_.size()) && pages_[page].w > 0 &&
        pages_[page].h > 0) {
        pw = pages_[page].w;
        ph = pages_[page].h;
    }
    const float s = std::min(boxW / pw, boxH / ph);
    *w = pw * s;
    *h = ph * s;
}

void ManualView::wantPages() {
    if (status_ != Status::Ready || pages_.empty()) return;
    const int count = static_cast<int>(pages_.size());
    std::vector<Key> want;
    want.push_back({page_, 1});
    if (zoomed_) want.push_back({page_, 2});
    if (page_ + 1 < count) want.push_back({page_ + 1, 1});
    if (page_ > 0) want.push_back({page_ - 1, 1});

    // Let go of everything else, except the page a turn is leaving.
    for (auto it = textures_.begin(); it != textures_.end();) {
        const bool keep = std::find(want.begin(), want.end(), it->first) != want.end() ||
                          (it->first.first == fromPage_ && turn_.value() < 1.0f);
        if (keep) { ++it; continue; }
        freeTexture(it->second);
        it = textures_.erase(it);
    }

    std::lock_guard<std::mutex> lk(m_);
    requests_.clear();
    for (const Key& k : want) {
        if (textures_.count(k)) continue;
        float w, h;
        fitted(k.first, &w, &h);
        int px = static_cast<int>(std::lround(w * pxPerPoint_ * static_cast<float>(k.second)));
        const int pyx = static_cast<int>(std::lround(h * pxPerPoint_ * static_cast<float>(k.second)));
        // No picture larger than the graphics chip takes.
        if (maxTexture_ > 0 && (px > maxTexture_ || pyx > maxTexture_)) {
            const float s = static_cast<float>(maxTexture_) /
                            static_cast<float>(std::max(px, pyx));
            px = static_cast<int>(static_cast<float>(px) * s);
        }
        requests_.push_back({k.first, k.second, std::max(1, px)});
    }
    cv_.notify_all();
}

void ManualView::panTo(float x, float y, bool animate) {
    float fw, fh;
    fitted(page_, &fw, &fh);
    const float z = zoomed_ ? kZoom : 1.0f;
    auto axis = [&](design::Animated& pan, float want, float size, float screen) {
        float target = 0.5f;
        if (size > screen) {
            const float half = screen * 0.5f / size;
            target = std::clamp(want, half, 1.0f - half);
        }
        pan.smooth = true;
        if (animate) pan.retarget(target, kPanTime);
        else pan.settle(target);
    };
    axis(panX_, x, fw * z, ui::kCanvasWidth);
    axis(panY_, y, fh * z, ui::kCanvasHeight);
}

void ManualView::showCount() {
    countFor_ = kCountHold;
    count_.retarget(1.0f, kCountFade);
}

void ManualView::turn(int dir) {
    const int next = page_ + dir;
    if (next < 0 || next >= static_cast<int>(pages_.size())) {
        sound::play(sound::Cue::Edge);
        return;
    }
    fromPage_ = page_;
    page_ = next;
    turnDir_ = dir;
    turn_.from = turn_.to = 0.0f;
    turn_.elapsed = 0.0f;
    turn_.smooth = true;
    turn_.retarget(1.0f, kTurnTime);
    // Zoomed, the next page starts at its top left, where reading starts.
    if (zoomed_) panTo(0.0f, 0.0f, false);
    sound::play(sound::Cue::Move);
    showCount();
    wantPages();
}

void ManualView::key(Nav n) {
    if (status_ != Status::Ready || pages_.empty()) {
        if (n != Nav::Back) sound::play(sound::Cue::Edge);
        return;
    }
    float fw, fh;
    fitted(page_, &fw, &fh);
    switch (n) {
        case Nav::Activate:
            zoomed_ = !zoomed_;
            zoom_.smooth = true;
            zoom_.retarget(zoomed_ ? kZoom : 1.0f, kZoomTime);
            // In, at the top left, where reading starts; out, centred.
            panTo(0.0f, 0.0f, true);
            sound::play(zoomed_ ? sound::Cue::Activate : sound::Cue::Back);
            wantPages();
            break;
        case Nav::Up:
        case Nav::Down: {
            const float size = fh * (zoomed_ ? kZoom : 1.0f);
            const float before = panY_.to;
            if (zoomed_ && size > ui::kCanvasHeight)
                panTo(panX_.to,
                      before + (n == Nav::Down ? 1.0f : -1.0f) * kPanStep * ui::kCanvasHeight / size,
                      true);
            sound::play(panY_.to != before ? sound::Cue::Move : sound::Cue::Edge);
            break;
        }
        case Nav::Left:
        case Nav::Right: {
            // Zoomed across a page wider than the screen, the press moves
            // across it first; at its edge, it turns.
            const float size = fw * (zoomed_ ? kZoom : 1.0f);
            const int dir = n == Nav::Right ? 1 : -1;
            if (zoomed_ && size > ui::kCanvasWidth) {
                const float before = panX_.to;
                panTo(before + static_cast<float>(dir) * kPanStep * ui::kCanvasWidth / size,
                      panY_.to, true);
                if (panX_.to != before) {
                    sound::play(sound::Cue::Move);
                    break;
                }
            }
            turn(dir);
            break;
        }
        case Nav::Back: break;
    }
}

void ManualView::tick(float dt) {
    if (!isOpen()) return;
    waited_ += dt;
    zoom_.tick(dt);
    panX_.tick(dt);
    panY_.tick(dt);
    turn_.tick(dt);
    count_.tick(dt);
    if (countFor_ > 0.0f) {
        countFor_ -= dt;
        if (countFor_ <= 0.0f) count_.retarget(0.0f, kCountFade);
    }
    for (auto& [k, t] : textures_) t.fade = std::min(1.0f, t.fade + dt / kPageFade);

    std::deque<Done> done;
    bool measured = false;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (sizesReady_) {
            sizesReady_ = false;
            if (sizes_.empty()) {
                std::fprintf(stderr, "[manual] %s will not open: %s\n", pdf_.c_str(),
                             sizesError_.c_str());
                status_ = Status::Failed;
            } else {
                pages_ = std::move(sizes_);
                status_ = Status::Ready;
                measured = true;
                std::fprintf(stderr, "[manual] %s: %zu pages\n", pdf_.c_str(), pages_.size());
            }
        }
        done.swap(done_);
    }
    // Measured: the first page, and the count with it.
    if (measured) {
        showCount();
        wantPages();
    }
    for (Done& d : done) {
        if (!d.ok) {
            if (d.page == page_ && !textures_.count({page_, 1})) status_ = Status::Failed;
            continue;
        }
        Texture t;
        t.width = d.picture.width;
        t.height = d.picture.height;
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        // Three bytes a pixel, so rows need not fall on four.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, t.width, t.height, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     d.picture.rgb.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        // A neighbour drawn ahead is there at full strength when it is
        // turned to; only the page on screen fades in.
        t.fade = d.page == page_ ? 0.0f : 1.0f;
        Texture& slot = textures_[{d.page, d.level}];
        freeTexture(slot);
        slot = t;
    }
    if (!done.empty()) wantPages();
}

void ManualView::draw(Ctx& c, float t) {
    if (!isOpen() || t <= 0.0f) return;
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight;
    const float keepX = c.r.contentOffsetX();
    c.r.setContentOffsetX(0.0f);
    c.r.draw(ui::Rect{0, 0, W, H, 0, ui::Color::black(kScrim * t)});
    const float slideX = (1.0f - t) * W;
    c.r.setContentOffsetX(slideX);

    auto say = [&](const char* words) {
        const float w = c.text.measure(words, ui::TextStyle::Title3, c.sc);
        c.text.draw(c.r, words, (W - w) * 0.5f, H * 0.5f, ui::TextStyle::Title3,
                    ui::Color::white(0.60f), c.sc);
    };
    if (status_ == Status::Failed) {
        say("Couldn't open the manual");
        c.r.setContentOffsetX(keepX);
        return;
    }

    // One page, at the zoom and pan of the moment, shifted `dx` and faded.
    auto drawPage = [&](int page, float dx, float alpha) {
        const Texture* tex = nullptr;
        const float z = zoom_.value();
        auto have = [&](int level) -> const Texture* {
            auto it = textures_.find({page, level});
            return it == textures_.end() ? nullptr : &it->second;
        };
        tex = z > 1.01f ? (have(2) ? have(2) : have(1)) : (have(1) ? have(1) : have(2));
        if (!tex) return false;
        float fw, fh;
        fitted(page, &fw, &fh);
        const float w = fw * z, h = fh * z;
        const bool current = page == page_;
        const float fx = current ? panX_.value() : 0.5f;
        const float fy = current ? panY_.value() : 0.5f;
        float x = W * 0.5f - fx * w + dx;
        float y = H * 0.5f - fy * h;
        // On whole screen pixels, so a page drawn at its own size stays sharp.
        x = std::round(x * c.sc) / c.sc;
        y = std::round(y * c.sc) / c.sc;
        c.r.drawTextured(x, y, w, h, tex->id, 0, 0, 1, 1,
                         ui::Color{1, 1, 1, alpha * tex->fade}, false);
        return true;
    };

    const float k = turn_.value();
    if (fromPage_ >= 0 && k < 1.0f)
        drawPage(fromPage_, -static_cast<float>(turnDir_) * kTurnShift * k, 1.0f - k);
    const float inDx = fromPage_ >= 0 && k < 1.0f
                           ? static_cast<float>(turnDir_) * kTurnShift * (1.0f - k)
                           : 0.0f;
    if (!drawPage(page_, inDx, fromPage_ >= 0 && k < 1.0f ? k : 1.0f) &&
        waited_ > kSayWaiting)
        say("Opening the manual\xE2\x80\xA6");
    c.r.setContentOffsetX(keepX);
}

void ManualView::drawGlass(Ctx& c, float t) {
    if (!isOpen() || t <= 0.0f || status_ != Status::Ready || pages_.empty()) return;
    const float a = count_.value();
    if (a <= 0.001f) return;
    const float keepX = c.r.contentOffsetX();
    c.r.setContentOffsetX((1.0f - t) * ui::kCanvasWidth);
    char words[48];
    std::snprintf(words, sizeof words, "%d of %zu", page_ + 1, pages_.size());
    const ui::TextStyle st = ui::TextStyle::Callout;
    const float tw = c.text.measure(words, st, c.sc);
    const float ph = c.text.lineHeight(st, c.sc) + 2.0f * design::kPillPadY;
    const float pw = tw + 2.0f * design::kPillPadX * 1.6f;
    const float px = (ui::kCanvasWidth - pw) * 0.5f;
    const float py = ui::kCanvasHeight - kMargin - 24.0f - ph;
    const float keepA = c.r.contentAlpha();
    c.r.setContentAlpha(a);
    c.r.drawGlass(ui::Rect{px, py, pw, ph, ph * 0.5f, ui::Color::white(0)},
                  design::kThinMaterialBlur, ui::Color::black(0.35f));
    c.text.draw(c.r, words, px + (pw - tw) * 0.5f,
                py + design::kPillPadY + c.text.ascent(st, c.sc), st, ui::Color::white(0.92f),
                c.sc);
    c.r.setContentAlpha(keepA);
    c.r.setContentOffsetX(keepX);
}

}  // namespace screens
