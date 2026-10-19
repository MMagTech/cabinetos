// Wii Remotes: the panel, Pair a Wii Remote and its window, and the panel
// kept in step with the Remotes (#200) (#398, restructure batch e).

#include "app.h"

#include <strings.h>

#include <algorithm>
#include <cstdio>

using namespace design;   // NOLINT, as main.cpp names them

void App::openWiiPairWindow() {
    wiiPair.quit();
    wiiPair.stop = false;
    {
        std::lock_guard<std::mutex> lk(wiiPair.m);
        wiiPair.fresh = wiiPair.done = false;
        wiiPair.address.clear();
    }
    wiiPairFirst = wiiremote::known().empty();
    askChoice("Pair a Wii Remote", "Press the red sync button", {}, 0, [](int) {});
    choiceScreen.setStaysOpen(true);
    choiceScreen.setFixedWidth(kPadWindowWidth);
    wiiPairOpen = true;
    wiiPair.running = true;
    wiiPair.th = std::thread([&job = wiiPair]() {
        struct Ended {
            std::atomic<bool>& r;
            ~Ended() { r = false; }
        } ended{job.running};
        auto say = [&](const std::string& d) {
            std::lock_guard<std::mutex> lk(job.m);
            job.detail = d;
            job.fresh = true;
        };
        while (!job.stop) {
            std::vector<std::string> found;
            std::string err;
            if (!wiiremote::search(&found, &err, &job.stop)) {
                if (job.stop) break;
                std::fprintf(stderr, "[wiiremote] search: %s\n", err.c_str());
                say("Couldn't search for Wii Remotes");
                break;
            }
            for (const std::string& address : found) {
                if (job.stop) break;
                say("Pairing\xE2\x80\xA6");
                if (wiiremote::pair(address, &err)) {
                    std::lock_guard<std::mutex> lk(job.m);
                    job.address = address;
                    job.done = true;
                    job.fresh = true;
                    return;
                }
                say("Couldn't pair");
            }
        }
    });
}

// NOT JOINED HERE (#481, SETTINGS-23): wiiremote::pair takes no stop, so a
// Back during "Pairing..." held the picture until the pairing ended, up to
// about a minute. The search stops at once; a pairing finishes behind the
// panel, and Pair a Wii Remote waits for it (openWiiPanelAt's press).
void App::closeWiiPairWindow() {
    wiiPair.stop = true;
    wiiPairOpen = false;
}

// BY PLAYER: the connected Remotes in light order, then any that are off
// (MMagTech, 2026-10-03; Bluetooth's own order put Player 2 above Player
// 1). Sorted when the panel is drawn; while it is open the values follow
// the Remotes in place, so a row never moves under the cursor.
std::vector<wiiremote::Paired> App::wiiPanelRemotes() {
    std::vector<wiiremote::Paired> rows = wiiremote::known();
    std::stable_sort(rows.begin(), rows.end(),
                     [](const wiiremote::Paired& a, const wiiremote::Paired& b) {
                         auto rank = [](const wiiremote::Paired& w) {
                             return w.light ? w.light : (w.connected ? 10 : 20);
                         };
                         return rank(a) < rank(b);
                     });
    return rows;
}

std::vector<std::string> App::wiiPanelValues(const std::vector<wiiremote::Paired>& rows) {
    std::vector<std::string> values;
    for (const wiiremote::Paired& w : rows)
        values.push_back(w.light ? "Player " + std::to_string(w.light)
                                 : (w.connected ? "Connected" : "Off"));
    values.push_back("");
    values.push_back(wiiremote::sensorBarAbove() ? "Above the TV" : "Below the TV");
    return values;
}

void App::openWiiPanelAt(int focus) {
    wiiRowsShown = wiiPanelRemotes();
    std::vector<std::string> names;
    for (size_t j = 0; j < wiiRowsShown.size(); ++j) names.push_back("Wii Remote");
    const std::vector<std::string> values = wiiPanelValues(wiiRowsShown);
    const int pairAt = static_cast<int>(names.size());
    names.push_back("Pair a Wii Remote");
    const int barAt = static_cast<int>(names.size());
    names.push_back("Sensor bar");
    askChoice("Wii Remotes", "", names, std::clamp(focus, 0, barAt), [&, pairAt, barAt](int i) {
        if (i == barAt) {
            wiiremote::setSensorBarAbove(!wiiremote::sensorBarAbove());
            sound::play(sound::Cue::Move);
            openWiiPanelAt(barAt);
            return;
        }
        if (i == pairAt) {
            if (wiiPair.running) {   // the last one's pairing is still ending
                sound::play(sound::Cue::Edge);
                openWiiPanelAt(pairAt);
                return;
            }
            choiceScreen.close();
            openWiiPairWindow();
            return;
        }
        if (i < 0 || i >= static_cast<int>(wiiRowsShown.size())) return;
        const wiiremote::Paired w = wiiRowsShown[static_cast<size_t>(i)];
        askChoice("Remove this Wii Remote?", "", {"Remove", "Cancel"}, 1, [&, w, i](int k) {
            if (k == 0) {
                std::string err;
                if (wiiremote::remove(w.address, &err))
                    std::fprintf(stderr, "[wiiremote] removed %s\n", w.address.c_str());
                else
                    menuNotice.say("Couldn't remove the Wii Remote", Tone::Problem);
                rebuildSettingsRows();
            }
            openWiiPanelAt(i);
        });
        // B goes back to the panel, as Cancel does.
        choiceBack = [&, i]() { openWiiPanelAt(i); };
    });
    choiceScreen.setValues(values);
    choiceScreen.setStaysOpen(true);
    wiiValuesShown = values;
}

void App::openWiiPanel() { openWiiPanelAt(0); }

// THE WII REMOTES PANEL FOLLOWS THE REMOTES while it is open: one
// switched on, off, or connecting a moment after its pairing (it
// read "Off" with its light on, MMagTech 2026-10-03). Values only,
// in place.
void App::followWiiRemotes() {
    if (choiceScreen.isOpen() && choiceScreen.title() == "Wii Remotes" &&
        here() == Screen::Settings) {
        std::vector<wiiremote::Paired> now = wiiremote::known();
        std::vector<wiiremote::Paired> inPlace;
        for (const wiiremote::Paired& shown : wiiRowsShown)
            for (const wiiremote::Paired& w : now)
                if (w.address == shown.address) inPlace.push_back(w);
        if (inPlace.size() == wiiRowsShown.size()) {
            std::vector<std::string> values = wiiPanelValues(inPlace);
            if (values != wiiValuesShown) {
                choiceScreen.setValues(values);
                wiiValuesShown = values;
                wiiRowsShown = inPlace;
            }
        }
    }
}

void App::pumpWiiPairing() {
    if (wiiPairOpen) {
        // Pair a Wii Remote: closed by Back, or paired. EITHER WAY BACK
        // TO THE WII REMOTES PANEL it was opened from, one step, as Back
        // goes everywhere else: on Pair a Wii Remote after Back, on the
        // new Remote after a pairing (MMagTech, 2026-10-03).
        if (!choiceScreen.isOpen() || here() != Screen::Settings) {
            const bool back = here() == Screen::Settings;
            choiceScreen.close();
            closeWiiPairWindow();
            if (back) openWiiPanelAt(static_cast<int>(wiiremote::known().size()));
        } else {
            bool fresh = false, done = false;
            std::string detail, address;
            {
                std::lock_guard<std::mutex> lk(wiiPair.m);
                fresh = wiiPair.fresh;
                wiiPair.fresh = false;
                done = wiiPair.done;
                detail = wiiPair.detail;
                address = wiiPair.address;
            }
            if (done) {
                choiceScreen.close();
                closeWiiPairWindow();
                menuNotice.say("Wii Remote paired", Tone::Done);
                buildSettings();
                int at = 0;
                const auto paired = wiiPanelRemotes();
                for (size_t j = 0; j < paired.size(); ++j)
                    if (strcasecmp(paired[j].address.c_str(), address.c_str()) == 0)
                        at = static_cast<int>(j);
                // THE SENSOR BAR, asked with the first Remote as a Wii's
                // own settings ask it; the panel's row changes it later.
                if (wiiPairFirst) {
                    askChoice("Sensor bar", "", {"Below the TV", "Above the TV"},
                              wiiremote::sensorBarAbove() ? 1 : 0, [&, at](int k) {
                        if (k >= 0) wiiremote::setSensorBarAbove(k == 1);
                        buildSettings();
                        openWiiPanelAt(at);
                    });
                    choiceBack = [&, at]() { openWiiPanelAt(at); };
                } else {
                    openWiiPanelAt(at);
                }
            } else if (fresh) {
                choiceScreen.replace({}, {}, detail);
            }
        }
    }
}
