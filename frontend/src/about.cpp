// About: How to, the page of what every control does (#314) (#398,
// restructure batch e).

#include "app.h"

#include <utility>

#include "shortcuts.h"

using namespace design;   // NOLINT, as main.cpp names them

void App::openHowTo() {
    using K = screens::SettingsRow::Kind;
    std::string button = "Home";
    if (SDL_Gamepad* gp = settingsLastPad ? SDL_GetGamepadFromID(settingsLastPad) : nullptr) {
        const std::string l = shortcuts::label(gp);
        if (!l.empty() && l != "Not set") button = l;
    }
    const bool on = shortcuts::enabled();
    auto line = [](const char* what, std::string how, K k = K::Info) {
        return screens::SettingsRow{k, 0, what, "", std::move(how)};
    };
    std::vector<screens::SettingsCategory> cats;
    cats.push_back({"In a game",
                    {line("Pause menu", "Both sticks in (L3 + R3)"),
                     line("Pause menu, Wii Remote", "Hold HOME"),
                     line("Pause menu, Remote Play", "Hold Select"),
                     line("Leave a game", "Pause menu, Exit to Home"),
                     line("Power menu", "The PC's power button")}});
    {
        std::vector<screens::SettingsRow> rows;
        if (!on) rows.push_back(line("Turn on", "Settings, Controllers, In-game shortcuts"));
        const K k = on ? K::Info : K::Disabled;
        rows.push_back(line("Pause menu", "Tap " + button, k));
        rows.push_back(line("Save state", button + " + R1", k));
        rows.push_back(line("Load state", button + " + L1", k));
        rows.push_back(line("Fast forward", button + " + R2", k));
        rows.push_back(line("Rewind", button + " + L2", k));
        rows.push_back(line("Screenshot", button + " + X", k));
        cats.push_back({"Shortcuts", std::move(rows)});
    }
    cats.push_back({"Menus",
                    {line("Select", "A"),
                     line("Back", "B"),
                     line("Change screen", "L1 / R1"),
                     line("Game manual", "R1, on a game's page"),
                     line("Jump a letter", "L2 / R2"),
                     line("Power menu", "Start, on Home"),
                     line("Switch person", "Up to the top bar, A on your name")}});
    cats.push_back({"Steam",
                    {line("Open", "Power menu, Switch to Steam"),
                     line("Come back", "Steam's Power menu, Switch to Desktop")}});
    cats.push_back({"Controllers",
                    {line("Pair a controller", "Settings, Controllers, Add a controller"),
                     line("Pair a Wii Remote", "The red sync button, under the battery cover")}});
    cats.push_back({"Drives", {line("Unplug", "Eject it first, in Settings, Storage")}});
    {
        std::string shown = kWikiUrl;
        shown = shown.substr(shown.find("//") + 2);
        if (!shown.empty() && shown.back() == '/') shown.pop_back();
        cats.push_back({"Wiki", {line("Address", shown)}, kWikiUrl});
    }
    howToScreen.setCategories(std::move(cats));
    howToScreen.enter();
    stack.push_back(Screen::HowTo);
}
