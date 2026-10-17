// A question with a few answers, over any screen (choice.h): asked, and its
// answer handed to whoever asked, with the slots that must follow the ask
// (#398, restructure batch e).

#include "app.h"

#include <utility>

using namespace design;   // NOLINT, as main.cpp names them

void App::askChoice(const std::string& title, const std::string& detail,
                    std::vector<std::string> options, int focus,
                    std::function<void(int)> then) {
    choiceThen = std::move(then);
    choiceStep = nullptr;
    choiceBack = nullptr;
    choiceScreen.open(title, detail, std::move(options), focus);
}

void App::choiceOutcome(screens::ChoiceScreen::Outcome o) {
    using O = screens::ChoiceScreen::Outcome;
    if (o == O::None) return;
    if (o == O::Stepped) {
        if (auto step = choiceStep) step(choiceScreen.stepped());
        return;
    }
    if (o == O::Cancelled && choiceBack) {
        auto back = std::move(choiceBack);
        choiceBack = nullptr;
        choiceThen = nullptr;
        choiceScreen.close();
        back();
        return;
    }
    if (o == O::Chosen && choiceScreen.staysOpen()) {
        // A copy: the answer may open another question over this one.
        if (auto then = choiceThen) then(choiceScreen.chosen());
        return;
    }
    choiceScreen.close();
    auto then = std::move(choiceThen);
    choiceThen = nullptr;
    if (o == O::Chosen && then) then(choiceScreen.chosen());
}
