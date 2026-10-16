// The diagnostic report: made on a worker, served while its screen is up,
// and stopped at exit (#398, restructure batch e).

#include "app.h"

#include <cstdio>

#include "storage.h"

using namespace design;   // NOLINT, as main.cpp names them

// THE DIAGNOSTIC REPORT: made, then served while its screen is up.
// Leaving the screen stops the link; a report finished after the
// screen was left is kept in File access and served to nobody.
void App::pollReport() {
    if (reportWorkerDone) {
        reportWorker.join();
        reportWorkerDone = false;
        if (here() == Screen::Report) {
            std::string url, why;
            if (!reportMade.ok) {
                reportScreen.setError("Couldn't create the report.");
            } else if (reportAddress.empty()) {
                reportScreen.setText("Diagnostic report", {"In File access, under reports."}, "");
                reportScreen.setError("No network, so it can't be downloaded.");
            } else if (!report::serve(reportMade.path, reportAddress, &url, &why)) {
                std::fprintf(stderr, "[report] could not serve: %s\n", why.c_str());
                reportScreen.setText("Diagnostic report", {"In File access, under reports."}, "");
                reportScreen.setError("Couldn't offer it for download.");
            } else {
                reportScreen.setText("Diagnostic report",
                                     {"Your phone has to be on this network.",
                                      "Also in File access, under reports."}, "");
                reportScreen.setPairing(url, url);
            }
        }
    }
    if (report::serving() && here() != Screen::Report) report::stop();
}

void App::stopReport() {
    if (reportWorker.joinable()) reportWorker.join();
    report::stop();
}

void App::pressReport() {
    if (reportWorker.joinable()) {
        sound::play(sound::Cue::Edge);
    } else {
        report::stop();
        reportScreen.setText("Diagnostic report", {}, "Creating\xE2\x80\xA6");
        reportScreen.open();
        stack.push_back(Screen::Report);
        reportWorkerDone = false;
        reportWorker = std::thread([&]() {
            reportMade = report::make(storage::root());
            std::string ip = net::status().ipv4;
            reportAddress = ip.substr(0, ip.find('/'));
            reportWorkerDone = true;
        });
        sound::play(sound::Cue::Activate);
    }
}
