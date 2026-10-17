// The account panel that hangs from the bar's chip: everybody else on this
// console, and Add an account (AccountScreen, accountpanel.h). Moved out of
// screens.cpp in the restructure (#398).

#include "accountpanel.h"

#include "panels.h"
#include "screenparts.h"
#include "sound.h"

namespace screens {

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

}  // namespace screens
