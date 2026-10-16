// The drives in Settings: Format and Eject, and the notices a drive
// arriving or going says (#236) (#398, restructure batch e).

#include "app.h"

#include <cstdio>

#include "storage.h"

using namespace design;   // NOLINT, as main.cpp names them

// FORMAT, from a drive's row. THREE STEPS, MMagTech 2026-09-25: the PIN
// if set; the drive by name and size, AND WHAT IS ON IT (#236, now that
// a drive with files on it can be formatted), with Cancel focused; then
// a code the console chose. The only thing on the console that erases
// anything; the worker checks it all again before it does.
void App::askFormat(const drives::Drive& d) {
    const std::string what =
        std::string(d.external ? "External" : "Internal") +
        (d.model.empty() ? "" : ", " + d.model) +
        (d.sizeBytes ? ", " + driveSize(static_cast<int64_t>(d.sizeBytes)) : "");
    askPin("Enter the PIN", "To format a drive", [&, d, what]() {
        // Counted now, not when Storage was drawn: a computer on File
        // access may have put files there since.
        const drives::Contents c = drives::contents(d);
        auto amount = [](int64_t b) {
            char buf[32];
            if (b >= 1000LL * 1000 * 1000) return driveSize(b);
            if (b >= 1000LL * 1000) std::snprintf(buf, sizeof buf, "%.0f MB", b / 1e6);
            else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1e3);
            return std::string(buf);
        };
        auto many = [](int64_t n, const char* one, const char* more) {
            return std::to_string(n) + " " + (n == 1 ? one : more);
        };
        std::string holds;
        if (!c.counted) holds = amount(c.bytes);
        else if (c.files > 0) holds = many(c.files, "file", "files") + ", " + amount(c.bytes);
        if (c.otherParts > 0) {
            const std::string parts = many(c.otherParts, "partition in another format",
                                           "partitions in other formats");
            holds = holds.empty() ? parts : holds + ", and " + parts;
        }
        holds = holds.empty() ? "Empty" : "Holds " + holds;
        std::fprintf(stderr, "[drives] format confirm for %s: %s\n", d.id.c_str(),
                     holds.c_str());
        askChoice("Format this drive?", what + "\n" + holds, {"Cancel", "Format"}, 0,
                  [&, d](int k) {
            if (k != 1) return;
            char code[8];
            std::snprintf(code, sizeof code, "%04u", static_cast<unsigned>(SDL_rand(10000)));
            askCode(code, std::string("Enter ") + code, "To format it as ext4", [&, d]() {
                std::fprintf(stderr, "[drives] format asked for %s\n", d.id.c_str());
                drives::format(d.id);
                buildSettings();
                // ITS OWN SCREEN UNTIL IT IS DONE (MMagTech, 2026-10-04):
                // the drive leaves Storage while it is formatted, and on
                // a slow USB stick that is two minutes of nothing. Closed
                // by the frame loop when the new drive is mounted.
                const std::string kind = d.external ? "External" : "Internal";
                askChoice("Formatting " + kind,
                          (d.model.empty() ? std::string() : d.model + ", ") +
                              driveSize(static_cast<int64_t>(d.sizeBytes)) +
                              "\nDon't unplug the drive",
                          {}, 0, nullptr);
                choiceScreen.setBusy(true);
            });
        });
    });
}

void App::followDrives() {
    // The Formatting screen goes when the format is done, the pill then
    // saying how it went.
    if (choiceScreen.isOpen() && choiceScreen.busy() && !drives::formatting() &&
        choiceScreen.title().rfind("Formatting ", 0) == 0)
        choiceScreen.close();
    // USB DRIVES coming, going and ejected. The pill and nothing else: no
    // sound, because a chime would play over a game. MMagTech, 2026-09-25.
    if (const drives::Notice dn = drives::poll(); dn.event != drives::Event::None) {
        const std::string drive = dn.external ? "External drive" : "Internal drive";
        switch (dn.event) {
            case drives::Event::Connected:
                menuNotice.say(drive + " connected", Tone::Done);
                break;
            case drives::Event::WrongFormat:
                menuNotice.say(drive + " isn't exFAT, NTFS or ext4", Tone::Problem);
                break;
            case drives::Event::CouldNotUse:
                menuNotice.say("Couldn't use the " + std::string(dn.external ? "external" : "internal") +
                                   " drive", Tone::Problem);
                break;
            case drives::Event::Removed:
                menuNotice.say(drive + " removed", Tone::Info);
                break;
            case drives::Event::SafeToUnplug:
                menuNotice.say("Safe to unplug", Tone::Done);
                break;
            case drives::Event::EjectFailed:
                menuNotice.say("Couldn't eject the external drive", Tone::Problem);
                break;
            case drives::Event::Formatted:
                menuNotice.say(drive + " formatted", Tone::Done);
                break;
            case drives::Event::FormatFailed:
                menuNotice.say("Couldn't format the drive", Tone::Problem);
                break;
            case drives::Event::None:
            case drives::Event::Changed:
                break;
        }
        // File access shows each drive as a folder; a drive that came or
        // went changes the list. Root rebuilds it (cabinetos-files refresh).
        // Changed covers an internal drive arriving or leaving, which has
        // no notice of its own.
        if (filesState.on && (dn.event == drives::Event::Connected ||
                              dn.event == drives::Event::Removed ||
                              dn.event == drives::Event::SafeToUnplug ||
                              dn.event == drives::Event::Changed))
            files::refreshDrives(nullptr);
        if (here() == Screen::Settings) buildSettings();
    }
}

void App::pressEject(const screens::Result& res) {
    const std::vector<std::string> locs = storage::locations();
    const size_t i = static_cast<size_t>(res.value - kSetEject);
    if (drives::ejecting() || i >= locs.size() || !storage::isExternal(locs[i])) {
        sound::play(sound::Cue::Edge);
        return;
    }
    // The drive's row was pressed: Eject is asked, with the
    // drive named, and Cancel beside it.
    const std::string loc = locs[i];
    const std::string ejName =
        driveNames.count(res.value) ? driveNames[res.value] : "External";
    // FORMAT BESIDE EJECT when nothing of the console's is on
    // the drive (#236). Eject stays first: it is what the row
    // says.
    std::optional<drives::Drive> fmt;
    if (auto f = ejectFormats.find(res.value);
        f != ejectFormats.end() && f->second < formatable.size() &&
        !drives::formatting())
        fmt = formatable[f->second];
    std::vector<std::string> opts = {"Eject", "Cancel"};
    if (fmt) opts = {"Eject", "Format", "Cancel"};
    askChoice(ejName, "", opts, 0, [&, loc, fmt](int k) {
    if (fmt && k == 1) {
        askFormat(*fmt);
        return;
    }
    if (k != 0) return;
    // FINISHES OR STOPS ANYTHING WRITING TO IT. The only thing
    // that writes to a drive is a download being kept there;
    // it is stopped and waited for, so nothing holds a file
    // open on the drive. Its stage is left as the worker set
    // it, so the frame loop undoes the keep as for any failed
    // download.
    if (launchJob.busy() && launchJob.entryPath.rfind(loc + "/", 0) == 0) {
        std::fprintf(stderr, "[drives] eject: stopping the download of %s\n",
                     launchJob.title.c_str());
        launchJob.stop();
    }
    drives::eject(loc);
    buildSettings();
    });
    sound::play(sound::Cue::Activate);
}

void App::pressFormat(const screens::Result& res) {
    const size_t i = static_cast<size_t>(res.value - kSetFormat);
    if (drives::formatting() || i >= formatable.size()) {
        sound::play(sound::Cue::Edge);
        return;
    }
    askFormat(formatable[i]);
    sound::play(sound::Cue::Activate);
}
