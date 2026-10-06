// A GAME'S ACHIEVEMENTS (#74): the list behind the game page's Achievements
// row, over the page in the pause menu's panel, as the choice panels are.
//
// Every achievement in the set with its badge, name, description and points:
// unlocked first, in colour; then locked, greyed, with RetroAchievements' own
// locked badge. It is for reading, so nothing in it is focused: Up and Down
// scroll it a row at a time and B closes it.

#pragma once

#include <string>
#include <vector>

#include "achievements.h"
#include "screens.h"

namespace screens {

class AchievementList {
public:
    void open(std::string title, ra::GameList list);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    // True when the press closed it.
    bool key(Nav n);
    void tick(float dt);
    void settle() { appear_.settle(1.0f); scroll_.settle(static_cast<float>(top_)); }
    void draw(Ctx& c);

private:
    bool open_ = false;
    std::string title_;
    ra::GameList list_;
    int top_ = 0;       // first row in view
    int shown_ = 6;     // rows that fit, worked out in draw()
    design::Animated appear_;
    design::Animated scroll_;
};

}  // namespace screens
