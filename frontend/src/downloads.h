// Settings' Downloads (#68): every downloaded game on this console, by system,
// and the one thing to do with them, which is remove them.
//
// DESIGNED WITH MMAGTECH 2026-09-26, docs/SETTINGS.md, Storage:
//
//   - TWO LEVELS, LIKE THE LIBRARY. The first is the systems, biggest first,
//     each with its count and size, and "Remove all" above them. A system
//     opens its games, biggest first. A drive full of NES games would be one
//     list of hundreds; grouped, the top level stays short however big the
//     collection.
//   - INSIDE A SYSTEM, A TICKS. Nothing is removed by one press: "Select all"
//     and "Remove" sit above the list, Remove greyed until something is
//     ticked, and every removal goes through one question naming how many
//     and how much. A 40 GB game cannot go by a stray A.
//   - A AND B ONLY. The whole console runs on the D-pad, A and B, and a
//     keyboard works everywhere; a Select button would be the first break.
//   - ONLY WHAT IS HERE. A game on a drive that is not connected is not
//     listed: it takes no room on this console, and it comes back into the
//     list with its drive (MMagTech, 2026-09-26). A system with games on
//     both shows the ones that are here.
//   - NO NAMES. Whose download it is is not shown and does not matter here:
//     removing takes it for everyone. A parent who minds sets the PIN, which
//     opening this asks for.
//
// A LAYER, LIKE THE QUESTION PANEL (choice.h) AND DRAWN THE SAME WAY, so it
// sits with Wi-Fi's networks and File access's panel. It holds the list it is
// handed and says what was asked for; the app asks "are you sure", removes,
// and hands the new list back.

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "screens.h"

namespace screens {

struct DownloadItem {
    int romId = 0;
    std::string title;
    std::string system;        // the platform, as the Library names it
    int64_t bytes = 0;
    std::string drive;         // "External", shown when there is more than one
};

// "1.9 GB", "96 MB", "80 KB": the unit that suits the number, as the game's
// own screen writes it.
std::string sizeText(int64_t bytes);
// "9 games · 12.4 GB", or "1 game" when nothing of it is on this console.
std::string countText(int games, int64_t bytes);

class DownloadsPanel {
public:
    enum class Outcome { None, Remove, Closed };

    void open(std::vector<DownloadItem> items);
    // After a removal: the new list. Stays in the system that was open while
    // it has games left, otherwise back to the systems; ticks are cleared.
    // Returns false when there is nothing left at all (the app closes it).
    bool replace(std::vector<DownloadItem> items);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }

    // After Remove: which games, and a title and detail for the question.
    const std::vector<int>& toRemove() const { return remove_; }
    const std::string& removeTitle() const { return removeTitle_; }
    const std::string& removeDetail() const { return removeDetail_; }
    bool removingAll() const { return removingAll_; }

    // For a capture: open the system at `index` in the systems list.
    void openSystem(int index);

    Outcome key(Nav n);
    void tick(float dt);
    void settle() { appear_.settle(1.0f); focus_.settle(1.0f); }
    void draw(Ctx& c);

private:
    struct System {
        std::string name;
        std::vector<int> items;   // indices into items_, biggest first
        int64_t bytes = 0;
    };
    void group();
    int actionCount() const;           // rows pinned above the list
    bool actionEnabled(int i) const;
    int listCount() const;
    int rowCount() const { return actionCount() + listCount(); }
    bool focusable(int row) const { return row >= actionCount() || actionEnabled(row); }
    void moveTo(int row);
    void scrollToFocus();
    int64_t tickedBytes() const;

    bool open_ = false;
    std::vector<DownloadItem> items_;
    std::vector<System> systems_;
    int system_ = -1;                  // the open system, -1 on the systems list
    std::set<int> ticked_;             // rom ids
    int row_ = 0;
    int top_ = 0;                      // first list row shown
    std::vector<int> remove_;
    std::string removeTitle_, removeDetail_;
    bool removingAll_ = false;
    design::Animated appear_;
    design::Animated focus_;
};

}  // namespace screens
