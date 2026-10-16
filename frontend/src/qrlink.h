// The link screen: a QR code and a code to approve on a phone. The app holds
// three, for adding an account, the Tailscale sign-in and the diagnostic
// report, and does the asking itself. Moved out of screens.h in the
// restructure (#398); what every screen shares is in screens.h.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

// --- Adding an account ------------------------------------------------------

// PAIRING SOMEBODY NEW, AND IT IS A SCREEN RATHER THAN PART OF THE PANEL.
// MMagTech agreed the split, 2026-09-21: *"when you click add user i agree to
// another screen."* The reason is the QR — a code has to be big enough to
// photograph from a sofa, and a 520-point panel hanging off the corner cannot
// hold one. The list and the switch stay in the panel; this takes the screen.
//
// IT SHOWS, IT DOES NOT PAIR. The app owns the client, the worker and the
// polling, for the same reason it owns the switch's refusals: this file does
// no networking and starts no threads.
class AddAccountScreen {
public:
    void open();

    // The server has issued a code. Encodes the QR here, on the frame thread,
    // because that is where the GL context is.
    void setPairing(const std::string& url, const std::string& code);
    // Before the server has answered, and again if it never does.
    void setBusy(bool on);
    void setError(const std::string& err);
    // THE SAME SCREEN SIGNS THE CONSOLE IN TO TAILSCALE (#286): a link to
    // approve on a phone, as adding an account is. Its own title and line;
    // adding an account keeps the ones below.
    void setText(std::string title, std::vector<std::string> lines, std::string busy);

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    std::string title_ = "Add an account";
    std::vector<std::string> lines_ = {"Sign in as the person you are adding,",
                                       "not as yourself."};
    std::string busyLine_ = "Getting a code\xE2\x80\xA6";
    std::string url_;
    std::string code_;
    std::string error_;
    bool busy_ = false;
    ui::QrTexture qr_;
    design::Animated appear_;
};

}  // namespace screens
