#include "screens.h"

#include "screenparts.h"
#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>

namespace screens {
namespace {

using design::Animated;
using design::Card;

}  // namespace

// --- Accounts ---------------------------------------------------------------

void AccountScreen::setRows(std::vector<AccountRow> rows) {
    rows_ = std::move(rows);
    slot_ = firstSelectable();
}

void AccountScreen::setAnchor(float rightX, float topY) {
    anchorRight_ = rightX;
    anchorTop_ = topY;
}

int AccountScreen::firstSelectable() const {
    // The first other account, or the Add row when there is nobody else —
    // which is every console with one account, so it is the common case and
    // the reason this panel is never a page of things you cannot press.
    return rows_.empty() ? 0 : 0;
}

void AccountScreen::open() {
    slot_ = firstSelectable();
    notice_.clear();
    appear_.retarget(0.0f, 0.0f);
    appear_.retarget(1.0f, 0.24f);
    focus_.retarget(1.0f, design::kFocusDuration);
}

void AccountScreen::setNotice(std::string s) { notice_ = std::move(s); }

void AccountScreen::tick(float dt) {
    focus_.tick(dt);
    appear_.tick(dt);
}

Result AccountScreen::key(Nav n) {
    auto step = [&](int d) -> Result {
        int at = slot_;
        for (int tries = 0; tries < rowCount(); ++tries) {
            at += d;
            // Sounds as every list does (#389): the end of it is Edge.
            if (at < 0 || at >= rowCount()) { sound::play(sound::Cue::Edge); return {}; }
            {
                if (at != slot_) {
                    slot_ = at;
                    focus_.retarget(0.0f, 0.0f);
                    focus_.retarget(1.0f, design::kFocusDuration);
                    sound::play(sound::Cue::Move);
                } else {
                    sound::play(sound::Cue::Edge);
                }
                return {};
            }
        }
        return {};
    };

    switch (n) {
        case Nav::Up:   return step(-1);
        case Nav::Down: return step(1);
        case Nav::Back: return {Action::Back, 0};
        case Nav::Activate:
            if (slot_ < 0 || slot_ >= rowCount()) return {};
            if (isAddRow(slot_)) {
                if (!addEnabled_) { sound::play(sound::Cue::Edge); return {}; }
                return {Action::AddAccount, 0};
            }
            // THE ID, NOT THE ROW. A list that changed underneath this screen
            // must not be able to switch the console to the wrong person.
            return {Action::SwitchAccount, rows_[slot_].id};
        default: return {};
    }
}

void AccountScreen::draw(Ctx& c) {
    const float a = appear_.value();
    // **SIZED AGAINST THE CHIP, because it hangs off it.** MMagTech, 2026-09-22:
    // *"that box for adding users and switching is way too big especially
    // compared to the logged user."* It was — the rows were Title3 at 38pt
    // with 60pt discs, opening from a chip that is Caption1 at 25pt with a
    // 26pt disc. The thing you pressed was less than half the scale of the
    // thing it produced, which is what made it read as a box landing on the
    // screen rather than as the chip opening.
    //
    // A row is a list item and the chip is an indicator, so a row is allowed
    // to be a little larger — but a little. Callout against the chip's
    // Caption1 is one step on the ramp, and a 36pt disc against its 26.
    const ui::TextStyle rowStyle = ui::TextStyle::Callout;
    const float rowH = c.text.lineHeight(rowStyle, c.sc) + 26.0f;
    const float discD = rowH - 22.0f;
    const float w = 400.0f;
    // HUNG FROM THE CHIP, not centred. The panel's right edge lines up with the
    // chip's, so it reads as the chip opening rather than as a screen arriving.
    const float x = anchorRight_ - w;
    const float top = anchorTop_;

    // The panel grows downward as it appears, which is what makes it read as an
    // expansion. Everything inside is clipped to it by being drawn after.
    const int rows = rowCount();
    // THE NOTICE WRAPS INSIDE THE PANEL. On one line "vivian was added.
    // Choose them to switch." ran off the panel's right edge on the A9.
    const std::vector<std::string> noticeLines =
        notice_.empty() ? std::vector<std::string>{}
                        : wrapTwoLines(c.text, notice_, ui::TextStyle::Callout, c.sc, w - 36.0f);
    const float noticeLineH = c.text.lineHeight(ui::TextStyle::Callout, c.sc);
    const float bodyH = rows * rowH + (rows - 1) * 8.0f + 18.0f * 2.0f +
                        (noticeLines.empty() ? 0.0f
                                             : 8.0f + noticeLineH * noticeLines.size());
    // FROSTED, LIKE THE CHIP IT OPENS FROM, BUT LIFTED. This is a dropdown
    // off the account chip, not an interruption, so it stays in the chip's
    // material; the pop-ups' dark panel was tried and felt too heavy for it
    // (MMagTech, 2026-09-24: "something feels wrong about this one
    // matching"). What made the frosted version melt into the Settings rows
    // was having no depth, not the material: so a denser frost than the
    // rows (0.18 against their 0.08), and the pop-ups' shadow and hairline
    // edge to put it on top.
    {
        ui::Rect lift{x, top, w, bodyH * a, design::kRowRadius, ui::Color::white(0)};
        lift.shadowBlur = design::kOverlayPanelShadowBlur;
        lift.shadowOffsetY = design::kOverlayPanelShadowY;
        lift.shadowColor = ui::Color::black(design::kOverlayPanelShadowAlpha * a);
        c.r.draw(lift);
    }
    c.r.drawGlass(ui::Rect{x, top, w, bodyH * a, design::kRowRadius, ui::Color::white(0)},
                  design::kRegularMaterialBlur, ui::Color::white(0.18f * a));
    {
        ui::Rect edge{x, top, w, bodyH * a, design::kRowRadius, ui::Color::white(0)};
        edge.border = design::kOverlayPanelBorder;
        edge.borderColor = ui::Color::white(design::kOverlayPanelBorderAlpha * 1.4f * a);
        c.r.draw(edge);
    }

    float y = top + 18.0f;

    for (int i = 0; i < rows; ++i) {
        const bool on = (i == slot_);
        const float f = on ? focus_.value() : 0.0f;
        // THE FOCUSED ROW AT THE DESIGN SYSTEM'S FOCUSED TINT, grown a little,
        // and the others' names dimmed. It was a 20% wash on a panel already
        // tinted 10%, with every name equally bright. MMagTech, 2026-09-24:
        // *"a bit too dim and i cant make out who is selected if anything"*.
        const float gs = 1.0f + f * (design::kRowFocusScale - 1.0f);
        const float rw0 = w - 16.0f;
        const float rw = rw0 * gs, rh = rowH * gs;
        const float rx = x + 8.0f - (rw - rw0) * 0.5f;
        c.r.draw(ui::Rect{rx, y - (rh - rowH) * 0.5f, rw, rh, design::kRowRadius,
                          ui::Color::white((0.04f + (design::kFocusedTint - 0.04f) * f) * a)});
        const float nameA = 0.70f + 0.30f * f;

        if (isAddRow(i)) {
            // A PLUS ON A DISC, so it sits in the same column as the faces and
            // reads as one more entry in the same list rather than as a button
            // bolted underneath it.
            const float dx = x + 20.0f, dy = y + (rowH - discD) * 0.5f;
            c.r.draw(ui::Rect{dx, dy, discD, discD, discD * 0.5f,
                              ui::Color::white(0.14f * a)});
            const float pw = c.text.measure("+", rowStyle, c.sc);
            c.text.draw(c.r, "+", dx + (discD - pw) * 0.5f,
                        dy + (discD - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                            c.text.ascent(rowStyle, c.sc),
                        rowStyle, ui::Color::white(0.8f * (addEnabled_ ? 1.0f : 0.4f) * a), c.sc);
            c.text.draw(c.r, "Add an account", dx + discD + 14.0f,
                        y + (rowH - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                            c.text.ascent(rowStyle, c.sc),
                        rowStyle,
                        ui::Color::white(nameA * (addEnabled_ ? 1.0f : 0.4f) * a), c.sc);
            y += rowH + 8.0f;
            continue;
        }

        const AccountRow& row = rows_[i];
        // The disc is drawn either way: the ground under a picture with
        // transparency, and the fallback when there is none. Same rule as the
        // chip in the bar, and the same reason.
        const float dx = x + 24.0f, dy = y + (rowH - discD) * 0.5f;
        c.r.draw(ui::Rect{dx, dy, discD, discD, discD * 0.5f, ui::Color::white(0.22f * a)});
        const ui::Image* face = nullptr;
        if (!row.avatar.empty()) {
            const ui::Image& img = c.images.get(row.avatar);
            if (img.ready) face = &img;
        }
        if (face) {
            ui::drawImage(c.r, *face, dx, dy, discD, discD, ui::Fit::Fill,
                          face->fade * a, discD * 0.5f);
        } else if (!row.name.empty()) {
            const std::string initial(1, static_cast<char>(std::toupper(
                static_cast<unsigned char>(row.name[0]))));
            const float iw = c.text.measure(initial, rowStyle, c.sc);
            c.text.draw(c.r, initial, dx + (discD - iw) * 0.5f,
                        dy + (discD - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                            c.text.ascent(rowStyle, c.sc),
                        rowStyle, ui::Color::white(0.85f * a), c.sc);
        }

        c.text.draw(c.r, row.name, dx + discD + 14.0f,
                    y + (rowH - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                        c.text.ascent(rowStyle, c.sc),
                    rowStyle, ui::Color::white(nameA * a), c.sc);
        y += rowH + 8.0f;
    }

    float nb = y + c.text.ascent(ui::TextStyle::Callout, c.sc);
    for (const std::string& line : noticeLines) {
        c.text.draw(c.r, line, x + 18.0f, nb, ui::TextStyle::Callout,
                    ui::Color::white(0.85f * a), c.sc);
        nb += noticeLineH;
    }
}

// --- Adding an account ------------------------------------------------------

void AddAccountScreen::open() {
    url_.clear();
    code_.clear();
    error_.clear();
    busy_ = true;
    appear_.retarget(0.0f, 0.0f);
    appear_.retarget(1.0f, 0.3f);
}

void AddAccountScreen::setPairing(const std::string& url, const std::string& code) {
    url_ = url;
    code_ = code;
    busy_ = false;
    std::string err;
    // THE QR IS THE SERVER'S URL AND NOT A SHAPE WE GUESSED. `romm.h` records
    // what happens otherwise: a fabricated one scans perfectly and lands on a
    // page saying the code does not exist.
    const qr::Code c = qr::encode(url, &err);
    if (c.valid()) qr_.set(c);
    else error_ = err;   // the address and the code below are still usable
}

void AddAccountScreen::setText(std::string title, std::vector<std::string> lines,
                               std::string busy) {
    title_ = std::move(title);
    lines_ = std::move(lines);
    busyLine_ = std::move(busy);
}

void AddAccountScreen::setBusy(bool on) { busy_ = on; }
void AddAccountScreen::setError(const std::string& err) { error_ = err; busy_ = false; }

void AddAccountScreen::tick(float dt) { appear_.tick(dt); }

Result AddAccountScreen::key(Nav n) {
    // ONE WAY OUT AND IT IS BACK. Nothing here is a choice — it is a code
    // somebody is copying onto a phone — so a focus ring would have nowhere to
    // go and would only invite a press that does nothing.
    if (n == Nav::Back) return {Action::Back, 0};
    return {};
}

void AddAccountScreen::draw(Ctx& c) {
    const float a = appear_.value();

    // THE SAME SHAPE AS FIRST RUN'S PAIRING STEP, and the first version of this
    // screen was not. MMagTech, 2026-09-22: *"the add user screen seemed way
    // too jarring visually in presentation and text."* It was bare text on the
    // gradient — the only screen in the console with no material under it —
    // with a 76pt title and a 76pt code competing, and a raw URL at 38pt bold
    // dominating the middle of it.
    //
    // setup.cpp already solved this screen: prose on the left, the thing you
    // act on in a panel on the right, one line along the bottom. Its own
    // comment says why the shape is constant — *"so the flow does not appear
    // to jump between five unrelated screens"* — and adding somebody is the
    // same job as pairing the first somebody. The numbers below are its
    // numbers, deliberately.
    constexpr float kInset = 80.0f;
    constexpr float kTitleTop = 150.0f;
    constexpr float kProseWidth = 760.0f;
    constexpr float kPanelX = 1020.0f;
    constexpr float kPanelY = 168.0f;
    constexpr float kPanelW = ui::kCanvasWidth - kPanelX - kInset;
    constexpr float kPanelH = 660.0f;
    constexpr float kFooterY = 900.0f;

    // --- the left column: what is happening and why --------------------------
    float y = kTitleTop;
    c.text.draw(c.r, title_, kInset,
                y + c.text.ascent(ui::TextStyle::LargeTitle, c.sc),
                ui::TextStyle::LargeTitle, ui::Color::white(0.96f * a), c.sc);
    y += c.text.lineHeight(ui::TextStyle::LargeTitle, c.sc) + 28.0f;

    // PROSE IS `Body`, WHICH IS WHAT setup.cpp USES AND WHAT THIS SCREEN DID
    // NOT. It was Title3 — 38pt against the 76pt title — and two near-headline
    // sizes stacked is most of why it read as shouting.
    //
    // Written as short lines rather than wrapped, because `wrap` lives in
    // setup.cpp behind `hardWrap` and this does not need either: the strings
    // are fixed, they are mine, and they are well inside a 760pt column at
    // 29pt. **If a line here ever grows, measure it** — that is the rule a
    // truncated tile caption already bought once.
    const std::vector<std::string> lines =
        busy_ ? std::vector<std::string>{busyLine_} : lines_;
    for (const std::string& line : lines) {
        c.text.draw(c.r, line, kInset, y + c.text.ascent(ui::TextStyle::Body, c.sc),
                    ui::TextStyle::Body, ui::Color::white(0.72f * a), c.sc);
        y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
    }

    // THE ADDRESS AND THE CODE GO IN THE PROSE COLUMN, and putting them in the
    // panel was the second mistake on this screen. setup.cpp says why in its
    // own words: *"the pairing step puts the address and the code in the prose
    // column, because they are the things somebody reads out or types — the QR
    // is only a shortcut past typing them."* In the panel they were also white
    // text over a light glass card, which is the contrast the QR's own white
    // background creates, and the address was touching the panel's bottom edge.
    if (!code_.empty()) {
        y += 30.0f;
        if (!url_.empty()) {
            // THE WHOLE LINK, code and all, as first run shows it. It was
            // shown without `?user_code=`, and typed into a browser that
            // opens RomM's page with nowhere to put the code (MMagTech, with
            // no phone to scan the QR, 2026-10-06).
            std::string shown = url_;
            if (shown.rfind("http://", 0) == 0) shown = shown.substr(7);
            c.text.draw(c.r, shown, kInset,
                        y + c.text.ascent(ui::TextStyle::Body, c.sc),
                        ui::TextStyle::Body, ui::palette::kScreenCyan, c.sc);
            y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
        }
        // NO SEPARATE "Code" LINE: the link carries the code, and showing it
        // twice read as a mistake (MMagTech, 2026-10-06).
    }

    // AN OUTCOME BELONGS IN THE PROSE COLUMN, not over the code. This is where
    // "nobody was added" lands, and it is the whole reason that case stays on
    // this screen instead of returning to a panel that looks unchanged.
    if (!error_.empty()) {
        y += 30.0f;
        std::string rest = error_;
        for (int guard = 0; guard < 6 && !rest.empty(); ++guard) {
            size_t cut = rest.size();
            while (cut > 0 &&
                   c.text.measure(rest.substr(0, cut), ui::TextStyle::Callout, c.sc) > kProseWidth) {
                const size_t sp = rest.rfind(' ', cut - 1);
                if (sp == std::string::npos) break;
                cut = sp;
            }
            c.text.draw(c.r, rest.substr(0, cut), kInset,
                        y + c.text.ascent(ui::TextStyle::Callout, c.sc),
                        ui::TextStyle::Callout, ui::Color::white(0.92f * a), c.sc);
            y += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
            rest = (cut >= rest.size()) ? std::string() : rest.substr(cut + 1);
        }
    }

    // --- the right column: the thing you act on ------------------------------
    //
    // **NO GLASS PANEL BEHIND THE CODE, AND THERE WAS ONE.** MMagTech, looking
    // at it: *"why the giant white box around the qrcode."* Because there were
    // two boxes — a white card inside a light glass panel — and only one of
    // them earns its place.
    //
    // THE WHITE CARD IS NOT DECORATION. It is the quiet zone: four modules of
    // real white around the symbol, which `qr.h` records as measured rather
    // than assumed — the same code drawn flush to its edge does not decode at
    // all, and with the margin it decodes every time. It cannot go.
    //
    // The panel could, and did. It was copied from setup.cpp's shape, where
    // the same panel also holds rows of networks and controllers at the other
    // steps; here it only ever holds the QR, so it was a box around a card
    // holding nothing else. The card is the material on this side now.

    if (code_.empty()) return;

    // Everything that is words lives in the column on the left; this side is
    // the shortcut past typing them. Bigger than it was, because it no longer
    // has to leave room around itself inside something else — and a code that
    // is photographed from a sofa cannot be too large.
    const float side = 560.0f;
    const float qx = kPanelX + (kPanelW - side) * 0.5f;
    const float qy = kPanelY + (kPanelH - side) * 0.5f;
    if (qr_.valid()) qr_.draw(c.r, qx, qy, side);

    // --- NOTHING ALONG THE BOTTOM, and both lines that were here are gone ---
    //
    // "They are added to this console. Switching to them is separate." was
    // dropped at MMagTech's request: it answers a question nobody has asked
    // yet, at the moment they are trying to scan a code, and the panel names
    // who was added the instant it happens.
    //
    // **"B to go back" WAS WORSE — IT WAS WRONG.** MMagTech: *"b doesnt let me
    // go back like it suggests."* It does not, because SDL maps face buttons
    // by POSITION: `SDL_GAMEPAD_BUTTON_EAST` is Back everywhere in this
    // product, and on a Switch Pro Controller the east button is physically
    // **A**. South is Activate, and south is B. The screen was naming the one
    // button that does the opposite of what it claimed.
    //
    // **SO NOTHING HERE NAMES A PHYSICAL BUTTON.** No other screen in this
    // product does, and this is why: the letter depends on the pad, and a
    // console that will meet Switch, Xbox and PlayStation controllers cannot
    // put one in a string. If a hint is ever wanted here it has to come from
    // the pad SDL actually reports, not from a constant.
}

}  // namespace screens
