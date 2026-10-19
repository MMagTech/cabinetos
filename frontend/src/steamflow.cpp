// Steam: the tile, installing it, its share of the drive, and the jobs
// that do the slow parts (#223) (#398, restructure batch e).

#include "app.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "storage.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

bool App::runSteamJob(const std::string& busy, std::function<void()> work,
                      std::function<void()> finish) {
    // One at a time, and a second is refused out loud (#485, SETTINGS-11):
    // Adjust storage or Remove Steam pressed during a grow or a removal
    // closed the panel and did nothing, with no sound.
    if (steamJob.th.joinable()) {
        sound::play(sound::Cue::Edge);
        return false;
    }
    steamJob.done = false;
    steamJob.finish = std::move(finish);
    steamJob.busy = busy;
    steamJob.saidAt = SDL_GetTicks();
    if (!busy.empty()) menuNotice.say(busy, Tone::Busy);
    steamJob.th = std::thread([&j = steamJob, w = std::move(work)]() {
        w();
        j.done = true;
    });
    return true;
}

void App::pollSteamJob() {
    if (!steamJob.th.joinable()) return;
    if (!steamJob.done) {
        // The install panel's line: how much of Steam is on the drive.
        if (steamInstall && choiceScreen.isOpen() &&
            choiceScreen.title() == "Installing Steam") {
            const int64_t b = steamInstall->bytes.load();
            // One decimal under 10 GB, so the line visibly moves on a
            // slow connection ("1.2 GB"); driveSize's whole GB above.
            char gb[32] = "";
            if (b > 0 && b < 10'000'000'000LL)
                std::snprintf(gb, sizeof gb, "%.1f GB", static_cast<double>(b) / 1e9);
            choiceScreen.setDetail(b <= 0 ? std::string()
                                          : gb[0] ? std::string(gb) : driveSize(b));
        }
        // A Busy pill lasts fifteen seconds; a grow over a big slice takes
        // longer, and the pill must not go quiet while it works.
        if (!steamJob.busy.empty() && SDL_GetTicks() - steamJob.saidAt > 10000) {
            menuNotice.say(steamJob.busy, Tone::Busy);
            steamJob.saidAt = SDL_GetTicks();
        }
        return;
    }
    steamJob.th.join();
    auto finish = std::move(steamJob.finish);
    steamJob.finish = nullptr;
    if (finish) finish();
}

void App::readSteamSizes(SteamSizes* out) {
    out->current = steam::sliceBytes();
    out->room = steam::roomBytes();
    out->size = out->current > 0 ? out->current : steam::defaultBytes(out->room);
    const std::vector<std::string> locs = storage::locations();
    const storage::Space sp = storage::spaceOf(locs.empty() ? storage::root() : locs.front());
    out->drive = sp.totalBytes + out->current;
    out->online = net::status().online;
}

// "CabinetOS 1.55 TB": what the console keeps for itself beside Steam, the
// other side of the slider (MMagTech's brief: show both sides). Named as
// Settings, Storage names the main drive; "Games" read as games already
// installed (MMagTech on the TV, 2026-10-04).
std::string App::steamGamesLine(int64_t size) {
    return "CabinetOS " + driveSize(std::max<int64_t>(0, steamSizes.drive - size));
}

// Making the slice and handing over: Install on the storage screen.
// INSTALLING, WITH THE CONSOLE ON SCREEN (MMagTech on the TV, 2026-10-04:
// Steam's first start was a black screen for as long as its download took,
// "it still kind of looks like a stall"). One panel from Install to the
// handover: the slice is made (clearing cached games if it must), then
// Steam downloads itself on an invisible display (steam::download) while
// the panel shows how much is on the drive, and only then does the console
// hand over, to a Steam that opens in seconds. Cancel, or B, stops it and
// stays here; an install stopped or failed half way picks up where it left
// off the next time Steam is picked. Nothing is timed: it ends when Steam
// says so, on a slow connection or a fast one.
void App::installSteam(int64_t want) {
    auto job = std::make_shared<SteamInstall>();
    steamInstall = job;
    askChoice("Installing Steam", "", {}, 0, nullptr);
    choiceScreen.setPrompt("Press", "B", "to cancel");
    choiceBack = [job]() { job->cancel = true; };
    runSteamJob("", [job, want]() {
        if (!steam::isSetUp() && !steam::create(want, &job->why)) return;
        if (job->cancel) return;
        job->ok = steam::download(&job->cancel,
                                  [job](int64_t b) { job->bytes = b; }, &job->why);
    }, [&, job]() {
        if (steamInstall == job) steamInstall.reset();
        if (choiceScreen.isOpen() && choiceScreen.title() == "Installing Steam")
            choiceScreen.close();
        if (job->ok) {
            startLeaving(Leave::Steam, "Starting Steam");
        } else if (!job->cancel) {
            menuNotice.say(job->why.empty() ? std::string("Couldn't download Steam")
                                            : job->why,
                           Tone::Problem);
        }
    });
}

// The size panel: one row Left and Right change, 25 GB a press, with the
// console's side shown under the title (MMagTech's brief: both sides).
//
// INSTALLING, it is the second of the two install screens and goes both
// ways from 100 GB, since nothing exists yet; focus starts on Install, so
// (A) twice installs at the default and the size is there for anyone who
// looks (MMagTech on the TV, 2026-10-03: the first screen read as jargon).
// Back returns to the first screen. GROWING, from Storage, it only goes up
// (grow only, MMagTech 2026-10-03).
void App::showSteamSize(bool growing) {
    const int64_t step = steam::kStepBytes;
    const int64_t lo = growing ? steamSizes.current : steam::kMinBytes;
    const int64_t hi = steamSizes.room - steamSizes.room % step;
    auto shown = std::make_shared<int64_t>(
        growing ? std::min(hi, steamSizes.current - steamSizes.current % step + step)
                : steamSizes.size);
    if (growing && *shown <= steamSizes.current) *shown = steamSizes.current;
    auto refresh = [&, lo, hi, step, shown]() {
        choiceScreen.setValues({driveSize(*shown), ""});
        choiceScreen.setDetail(steamGamesLine(*shown));
        choiceScreen.setStepper(0, *shown - step >= lo, *shown + step <= hi);
    };
    askChoice(growing ? "Adjust storage" : "Steam storage", steamGamesLine(*shown),
              {"Steam", growing ? "Done" : "Install"}, growing ? 0 : 1,
              [&, growing, shown](int) {
        const int64_t want = *shown;
        if (!growing) {
            installSteam(want);
            return;
        }
        if (want <= steamSizes.current) return;
        runSteamJob("Freeing space\xE2\x80\xA6", [want]() {
            std::string why;
            steam::grow(want, &why);
        }, [&, want]() {
            rebuildSettingsRows();
            if (steam::sliceBytes() >= want - (1 << 20))
                menuNotice.say("Steam now has " + driveSize(steam::sliceBytes()), Tone::Done);
            else
                menuNotice.say("Couldn't change Steam's storage", Tone::Problem);
        });
    });
    refresh();
    choiceStep = [&, lo, hi, step, shown, refresh](int dir) {
        *shown = std::clamp<int64_t>(*shown + dir * step, lo, hi);
        refresh();
    };
    if (!growing) choiceBack = [&, shown]() {
        steamSizes.size = *shown;
        showSteamSetup();
    };
}

// The first install screen, as an install (MMagTech on the TV,
// 2026-10-03): Install (greyed while offline, since Steam downloads itself
// the first time, or when even 100 GB does not fit), Hide Steam, Cancel.
// The size is the next screen's.
void App::showSteamSetup() {
    const bool fits = steamSizes.size >= steam::kMinBytes;
    askChoice("Steam", fits ? "" : "Needs " + driveSize(steam::kMinBytes) + " of space",
              {"Install", "Hide Steam", "Cancel"}, 0, [&](int a) {
        if (a == 0) {
            showSteamSize(false);
        } else if (a == 1) {
            steam::setHidden(true);
            std::fprintf(stderr, "[steam] hidden\n");
        }
    });
    choiceScreen.setDisabled({!fits || !steamSizes.online, false, false});
}

// The tile. With a PIN set, anyone but the owner enters it first, and the
// PIN is the confirmation; otherwise "Start Steam?" catches a stray press
// (MMagTech, 2026-10-03). The first time, the first-pick screen instead.
void App::steamPressed() {
    if (steamJob.th.joinable()) {
        sound::play(sound::Cue::Edge);
        return;
    }
    const bool pinFirst = accounts::pinIsSet() && accounts::activeId() != accounts::ownerId();
    auto go = [&, pinFirst]() {
        if (steam::isSetUp() && !steam::installed()) {
            // An install stopped or failed half way: carry on with it.
            installSteam(steam::sliceBytes());
            return;
        }
        if (!steam::isSetUp()) {
            auto read = std::make_shared<SteamSizes>();
            runSteamJob("", [read]() { readSteamSizes(read.get()); },
                        [&, read]() {
                            steamSizes = *read;
                            showSteamSetup();
                        });
            return;
        }
        if (pinFirst) {
            startLeaving(Leave::Steam, "Starting Steam");
            return;
        }
        askChoice("Start Steam?", "", {"Start", "Cancel"}, 0, [&](int a) {
            if (a == 0) startLeaving(Leave::Steam, "Starting Steam");
        });
    };
    if (pinFirst) askPin("Enter the PIN", "To use Steam", go, true);
    else go();
}

void App::pressSteamShow() {
    askPin("Enter the PIN", "To show Steam", [&]() {
        steam::setHidden(false);
        std::fprintf(stderr, "[steam] shown\n");
                buildSettings();
    });
    sound::play(sound::Cue::Activate);
}

void App::pressSteam() {
    // Not while a grow or a removal is still running, as steamPressed.
    if (steamJob.th.joinable()) {
        sound::play(sound::Cue::Edge);
        return;
    }
    // Grow it, or remove Steam: the PIN when set, once per
    // visit, as Downloads (it gives or takes space for the
    // whole console).
    askPin("Enter the PIN", "To change Steam", [&]() {
        askChoice("Steam", driveSize(steam::sliceBytes()),
                  {"Adjust storage", "Remove Steam"}, 0, [&](int a) {
            if (a == 0) {
                auto read = std::make_shared<SteamSizes>();
                runSteamJob("", [read]() {
                    readSteamSizes(read.get());
                }, [&, read]() {
                    steamSizes = *read;
                    showSteamSize(true);
                });
                return;
            }
            askChoice("Remove Steam?", "", {"Remove", "Cancel"}, 1, [&](int b) {
                if (b != 0) return;
                auto ok = std::make_shared<bool>(false);
                runSteamJob("Removing Steam\xE2\x80\xA6", [ok]() {
                    std::string why;
                    *ok = steam::remove(&why);
                }, [&, ok]() {
                    buildSettings();
                                        menuNotice.say(*ok ? "Steam removed" : "Couldn't remove Steam",
                                   *ok ? Tone::Done : Tone::Problem);
                });
            });
        });
    });
    sound::play(sound::Cue::Activate);
}
