// File access and Developer access: on, off, their panels, and root's and
// systemd's answers (#69) (#398, restructure batch e).

#include "app.h"

#include <cstdio>
#include <ctime>

#include "prefs.h"
#include "unit.h"

using namespace design;   // NOLINT, as main.cpp names them

// File access on or off. A refusal is said in the row, as the update
// row does; otherwise "Turning on…" until root answers.
void App::setFiles(bool on) {
    std::string why;
    const bool ok = on ? files::turnOn(&why) : files::turnOff(&why);
    if (!ok) {
        filesState.on = false;
        filesState.failed = true;
        filesState.reason = why.find("not found") != std::string::npos ? "Not in this image" : why;
        if (filesState.reason.size() > 60) filesState.reason.resize(60);
        filesSaidAt = 0;
        filesPanelWhenOn = false;
    } else {
        filesSaidAt = std::time(nullptr);
    }
    filesWant = on;
    prefs::set("file_access", on ? "on" : "off");
    buildSettings();
}

void App::openFilesPanel() {
    std::string ip;
    {
        std::lock_guard<std::mutex> lk(settingsNet.m);
        if (settingsNet.have && !settingsNet.st.ipv4.empty())
            ip = settingsNet.st.ipv4.substr(0, settingsNet.st.ipv4.find('/'));
    }
    char host[256] = {0};
    ::gethostname(host, sizeof host - 1);
    std::string where = ip;
    if (host[0]) where += (where.empty() ? "" : " \xC2\xB7 ") + std::string(host) + ".local";
    const std::string pass = files::password();
    askChoice("File access",
              where + "\nUser name  cabinet\nPassword  " + (pass.empty() ? "\xE2\x80\xA6" : pass),
              {"Done", "New password", "Turn off"}, 0, [&](int k) {
                  if (k == 2) {
                      setFiles(false);
                  } else if (k == 1) {
                      // The old one stops working on every computer that
                      // saved it: asked once, focus on Cancel. Then the
                      // panel comes back with the new one.
                      askPin("Enter the PIN", "To make a new password", [&]() {
                          askChoice("New password?", "", {"New password", "Cancel"}, 1,
                                    [&](int c) {
                                        if (c != 0) { openFilesPanel(); return; }
                                        std::string why;
                                        if (!files::newPassword(&why)) {
                                            menuNotice.say("Couldn't make a new password",
                                                           Tone::Problem);
                                            return;
                                        }
                                        filesPanelWhenOn = true;
                                        filesSaidAt = std::time(nullptr);
                                    });
                      });
                  }
              });
}

// Developer access on or off, as File access: a refusal said in the row,
// otherwise "Turning on…" until systemd answers.
void App::setDeveloper(bool on) {
    std::string why;
    const bool ok = on ? unit::start(kDevUnit, &why) : unit::stop(kDevUnit, &why);
    if (!ok) {
        devFailed = why.find("not found") != std::string::npos ? "Not in this image" : why;
        if (devFailed.size() > 60) devFailed.resize(60);
        devSaidAt = 0;
        devPanelWhenOn = false;
    } else {
        devFailed.clear();
        devSaidAt = std::time(nullptr);
    }
    devWant = on;
    prefs::set("developer_access", on ? "on" : "off");
    buildSettings();
}

void App::openDevPanel() {
    std::string ip;
    {
        std::lock_guard<std::mutex> lk(settingsNet.m);
        if (settingsNet.have && !settingsNet.st.ipv4.empty())
            ip = settingsNet.st.ipv4.substr(0, settingsNet.st.ipv4.find('/'));
    }
    char host[256] = {0};
    ::gethostname(host, sizeof host - 1);
    std::string where = ip;
    if (host[0]) where += (where.empty() ? "" : " \xC2\xB7 ") + std::string(host) + ".local";
    const std::string pass = files::password();
    askChoice("Developer access",
              where + "\nPort  2222\nUser name  cabinet\nPassword  " +
                  (pass.empty() ? "\xE2\x80\xA6" : pass),
              {"Done", "New password", "Turn off"}, 0, [&](int k) {
                  if (k == 2) {
                      setDeveloper(false);
                  } else if (k == 1) {
                      askPin("Enter the PIN", "To make a new password", [&]() {
                          askChoice("New password?", "", {"New password", "Cancel"}, 1,
                                    [&](int c) {
                                        if (c != 0) { openDevPanel(); return; }
                                        std::string why;
                                        if (!files::newPassword(&why)) {
                                            menuNotice.say("Couldn't make a new password",
                                                           Tone::Problem);
                                            return;
                                        }
                                        devNewPassAt = std::time(nullptr);
                                    });
                      });
                  }
              });
}

// ---- File access (docs/SETTINGS.md, Storage; issue #69) -----------------
//
// Root's answer, read with the update status. `filesSaidAt` holds a state
// said on screen before root has written it ("Turning on…"), the way
// updSaidAt does. Whether it should be on is the owner's choice, kept in
// settings.json, so a console left with it on turns it on again at boot.
void App::startAccessAtBoot() {
    filesState = files::read();
    filesWant = prefs::get("file_access", "off") == "on";
    if (filesWant && !filesState.on) {
        std::string why;
        if (files::turnOn(&why)) filesSaidAt = std::time(nullptr);
        else std::fprintf(stderr, "[files] could not turn on at start: %s\n", why.c_str());
    }
    devState = unit::state(kDevUnit);
    devWant = prefs::get("developer_access", "off") == "on";
    // HIDDEN UNTIL UNLOCKED (MMagTech, 2026-10-07): the row is under About's
    // Version, and only once Version has been pressed seven times, the way
    // Android hides its developer options; seven more hide it again, and
    // hiding turns it off, so nothing can be listening behind a switch
    // nobody can see. A console with it on always shows it. Families never
    // see it, and on a console with no PIN a child on the owner's account
    // cannot find it to choose one. The PIN is still what protects it.
    devShown = devWant || prefs::get("developer_shown", "off") == "on";
    if (devWant && devState != "active") {
        std::string why;
        if (unit::start(kDevUnit, &why)) devSaidAt = std::time(nullptr);
        else std::fprintf(stderr, "[developer] could not turn on at start: %s\n", why.c_str());
    }
}

void App::pollAccess() {
    {
        // File access: root's answer. A state said on screen waits
        // for an answer written after it; ten seconds without one
        // and it gives up waiting and shows what root last said.
        const files::State f = files::read();
        const bool answered = filesSaidAt == 0 || f.at >= filesSaidAt ||
                              std::time(nullptr) - filesSaidAt > 10;
        if (answered && (f.on != filesState.on || f.failed != filesState.failed ||
                         f.reason != filesState.reason || f.at != filesState.at ||
                         filesSaidAt != 0)) {
            filesState = f;
            filesSaidAt = 0;
            std::fprintf(stderr, "[files] %s%s\n",
                         f.on ? "on" : (f.failed ? "failed: " : "off"), f.reason.c_str());
            if (here() == Screen::Settings) buildSettings();
            if (filesPanelWhenOn) {
                filesPanelWhenOn = false;
                if (f.on && here() == Screen::Settings && !choiceScreen.isOpen() &&
                    !pinScreen.isOpen())
                    openFilesPanel();
            }
        }
    }
    if (devSaidAt != 0 || devNewPassAt != 0 || here() == Screen::Settings) {
        // Developer access: systemd's answer. A change said on screen
        // waits until the unit has settled the way it was asked to
        // (or failed); ten seconds without that and it shows what
        // systemd says, with the failure in the row.
        const std::string d = unit::state(kDevUnit);
        const int64_t now = std::time(nullptr);
        const bool settled = d == "active" || d == "inactive" || d == "failed";
        const bool answered = devSaidAt == 0 || now - devSaidAt > 10 ||
                              (settled && (d == "failed" || (d == "active") == devWant));
        if (answered && (d != devState || devSaidAt != 0)) {
            if (devSaidAt != 0 && devWant && d != "active") devFailed = "Couldn't start SSH";
            devState = d;
            devSaidAt = 0;
            std::fprintf(stderr, "[developer] %s\n", d.c_str());
            if (here() == Screen::Settings) buildSettings();
            if (devPanelWhenOn) {
                devPanelWhenOn = false;
                if (d == "active" && here() == Screen::Settings && !choiceScreen.isOpen() &&
                    !pinScreen.isOpen())
                    openDevPanel();
            }
        }
        if (devNewPassAt != 0 && now - devNewPassAt >= 1) {
            devNewPassAt = 0;
            if (here() == Screen::Settings && !choiceScreen.isOpen() && !pinScreen.isOpen())
                openDevPanel();
        }
    }
}
