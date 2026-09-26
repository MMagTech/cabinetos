#include "players.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace players {

// ---- The rules --------------------------------------------------------------

int Seats::playerOf(uint32_t id) const {
    if (id == 0) return -1;
    for (size_t i = 0; i < seats_.size(); ++i)
        if (seats_[i].id == id) return static_cast<int>(i);
    return -1;
}

uint32_t Seats::idOf(int player) const {
    if (player < 0 || player >= static_cast<int>(seats_.size())) return 0;
    return seats_[player].id;
}

int Seats::add(uint32_t id, const std::string& key) {
    if (id == 0) return -1;
    if (const int p = playerOf(id); p >= 0) return p;
    // A pad coming back to the game it left takes its own number back. Known
    // by its key, never by position: two people's pads can both be away.
    if (!key.empty())
        for (size_t i = 0; i < seats_.size(); ++i)
            if (seats_[i].id == 0 && seats_[i].key == key) {
                seats_[i].id = id;
                return static_cast<int>(i);
            }
    if (seats_.size() < static_cast<size_t>(kMax)) {
        seats_.push_back({id, key});
        return static_cast<int>(seats_.size()) - 1;
    }
    // Four seats and one held for a pad that has not come back: a fifth pad
    // takes it rather than being left out of the game.
    for (size_t i = 0; i < seats_.size(); ++i)
        if (seats_[i].id == 0) {
            seats_[i] = {id, key};
            return static_cast<int>(i);
        }
    return -1;
}

void Seats::remove(uint32_t id) {
    const int p = playerOf(id);
    if (p < 0) return;
    if (!inGame_) {
        seats_.erase(seats_.begin() + p);
        return;
    }
    seats_[p].id = 0;
    const bool anyOn = std::any_of(seats_.begin(), seats_.end(),
                                   [](const Seat& s) { return s.id != 0; });
    if (!anyOn) seats_.clear();
}

void Seats::closeUp() {
    seats_.erase(std::remove_if(seats_.begin(), seats_.end(),
                                [](const Seat& s) { return s.id == 0; }),
                 seats_.end());
}

void Seats::setInGame(bool on) {
    inGame_ = on;
    if (!on) closeUp();
}

bool Seats::swap(int a, int b) {
    if (inGame_) return false;
    const int n = static_cast<int>(seats_.size());
    if (a < 0 || b < 0 || a >= n || b >= n) return false;
    if (seats_[a].id == 0 || seats_[b].id == 0) return false;
    std::swap(seats_[a], seats_[b]);
    return true;
}

// ---- The console's pads -----------------------------------------------------

namespace {

Seats gSeats;
std::map<SDL_JoystickID, SDL_Gamepad*> gOpen;
int gGeneration = 0;

// WHAT A PAD IS, ACROSS A RECONNECT. The joystick id is new every time a pad
// connects, so it cannot say "this is the pad that was player one". The
// serial can: over Bluetooth it is the pad's own address. The device path is
// the fallback for a pad with no serial, which holds for a wired pad put back
// in the same socket.
std::string keyOf(SDL_Gamepad* gp) {
    if (const char* s = SDL_GetGamepadSerial(gp); s && *s) return std::string("serial:") + s;
    if (const char* p = SDL_GetGamepadPath(gp); p && *p) return std::string("path:") + p;
    return {};
}

// The player lights on pads that have them (a Switch Pro Controller's four
// dots, a DualSense's bar) show the number the console gave it.
void light() {
    for (const auto& [id, gp] : gOpen) {
        const int p = gSeats.playerOf(id);
        SDL_SetGamepadPlayerIndex(gp, p >= 0 ? p : -1);
    }
}

void changed(const char* what, SDL_JoystickID id) {
    ++gGeneration;
    light();
    std::string order;
    for (size_t i = 0; i < gSeats.seats().size(); ++i) {
        const Seat& s = gSeats.seats()[i];
        char buf[48];
        std::snprintf(buf, sizeof buf, "%s%zu=%s", i ? " " : "", i + 1,
                      s.id ? std::to_string(s.id).c_str() : "held");
        order += buf;
    }
    std::fprintf(stderr, "[players] %s pad %u; players: %s\n", what,
                 static_cast<unsigned>(id), order.empty() ? "none" : order.c_str());
}

}  // namespace

void added(SDL_JoystickID id) {
    if (gOpen.count(id)) return;
    SDL_Gamepad* gp = SDL_OpenGamepad(id);
    if (!gp) {
        std::fprintf(stderr, "[players] could not open pad %u: %s\n",
                     static_cast<unsigned>(id), SDL_GetError());
        return;
    }
    gOpen[id] = gp;
    gSeats.add(id, keyOf(gp));
    changed("connected", id);
}

void removed(SDL_JoystickID id) {
    auto it = gOpen.find(id);
    if (it == gOpen.end()) return;
    SDL_CloseGamepad(it->second);
    gOpen.erase(it);
    gSeats.remove(id);
    changed("disconnected", id);
}

void setInGame(bool on) {
    if (gSeats.inGame() == on) return;
    gSeats.setInGame(on);
    changed(on ? "game started," : "game ended,", 0);
}

bool swap(int a, int b) {
    if (!gSeats.swap(a, b)) return false;
    changed("swapped", 0);
    return true;
}

SDL_Gamepad* gamepad(int player) {
    const uint32_t id = gSeats.idOf(player);
    if (!id) return nullptr;
    auto it = gOpen.find(id);
    return it == gOpen.end() ? nullptr : it->second;
}

int playerOf(SDL_JoystickID id) { return gSeats.playerOf(id); }

int count() { return static_cast<int>(gSeats.seats().size()); }

std::vector<Pad> connected() {
    std::vector<Pad> out;
    for (size_t i = 0; i < gSeats.seats().size(); ++i) {
        const uint32_t id = gSeats.seats()[i].id;
        auto it = id ? gOpen.find(id) : gOpen.end();
        if (it == gOpen.end()) continue;
        Pad p;
        p.player = static_cast<int>(i);
        p.id = id;
        if (const char* n = SDL_GetGamepadName(it->second)) p.name = n;
        p.bluetooth = SDL_GetGamepadConnectionState(it->second) ==
                      SDL_JOYSTICK_CONNECTION_WIRELESS;
        if (p.bluetooth)
            if (const char* s = SDL_GetGamepadSerial(it->second); s && *s) p.address = s;
        out.push_back(std::move(p));
    }
    return out;
}

int generation() { return gGeneration; }

// ---- --players-test ---------------------------------------------------------

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    {
        Seats s;
        check(s.add(10, "a") == 0, "the first pad is player 1");
        check(s.add(11, "b") == 1, "the second is player 2");
        check(s.add(10, "a") == 0, "a pad already seated keeps its seat");
        s.remove(10);
        check(s.playerOf(11) == 0, "outside a game, player 1 going off makes player 2 player 1");
        s.remove(11);
        check(s.add(12, "b") == 0,
              "both off, and the pad that was player 2 is picked up: it is player 1");
    }
    {
        Seats s;
        s.add(10, "a");
        s.add(11, "b");
        s.setInGame(true);
        s.remove(10);
        check(s.playerOf(11) == 1, "in a game, player 1 going off leaves player 2 as player 2");
        check(s.idOf(0) == 0, "and player 1's seat is held empty");
        check(s.add(20, "a") == 0, "player 1's pad comes back, under a new id, as player 1");
        s.remove(11);
        check(s.add(21, "c") == 2, "a different pad does not take a held seat");
        s.setInGame(false);
        check(s.seats().size() == 2 && s.playerOf(21) == 1,
              "leaving the game closes up the held seat");
    }
    {
        Seats s;
        s.add(10, "a");
        s.add(11, "b");
        s.setInGame(true);
        s.remove(10);
        s.remove(11);
        check(s.seats().empty(), "in a game, every pad off frees every seat");
        check(s.add(22, "b") == 0, "and the first pad back is player 1, whichever it was");
        check(s.add(23, "a") == 1, "and the other is player 2");
    }
    {
        Seats s;
        s.add(10, "");
        s.setInGame(true);
        s.add(11, "");
        s.remove(10);
        check(s.add(12, "") == 2, "a pad with no key never claims a held seat");
    }
    {
        Seats s;
        for (uint32_t i = 1; i <= 4; ++i) s.add(i, "k" + std::to_string(i));
        check(s.add(5, "k5") == -1, "a fifth pad has no player");
        s.setInGame(true);
        s.remove(2);
        check(s.add(5, "k5") == 1, "but takes a held seat when all four are taken");
    }
    {
        Seats s;
        s.add(10, "a");
        s.add(11, "b");
        check(s.swap(0, 1) && s.playerOf(11) == 0 && s.playerOf(10) == 1,
              "giving player 2's pad the number 1 swaps the two");
        s.add(12, "c");
        s.remove(12);
        check(s.playerOf(11) == 0, "and the swap holds while the numbers close up");
        check(!s.swap(0, 5), "a number with no pad is refused");
        s.setInGame(true);
        check(!s.swap(0, 1), "a swap during a game is refused");
    }

    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

// ---- --pads -----------------------------------------------------------------

int report(int seconds) {
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    auto describe = [](SDL_JoystickID id) {
        const bool known = SDL_IsGamepad(id);
        std::printf("pad %u  %s\n", static_cast<unsigned>(id),
                    known ? "known to SDL" : "NOT KNOWN TO SDL (joystick only)");
        std::printf("  name     %s\n", SDL_GetJoystickNameForID(id) ? SDL_GetJoystickNameForID(id) : "");
        std::printf("  path     %s\n", SDL_GetJoystickPathForID(id) ? SDL_GetJoystickPathForID(id) : "");
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof guid);
        std::printf("  guid     %s\n  vid:pid  %04x:%04x\n", guid,
                    SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
        if (known) {
            if (SDL_Gamepad* gp = SDL_OpenGamepad(id)) {
                const char* serial = SDL_GetGamepadSerial(gp);
                const SDL_JoystickConnectionState cs = SDL_GetGamepadConnectionState(gp);
                std::printf("  serial   %s\n  link     %s\n  type     %d\n", serial ? serial : "(none)",
                            cs == SDL_JOYSTICK_CONNECTION_WIRELESS ? "wireless"
                            : cs == SDL_JOYSTICK_CONNECTION_WIRED  ? "wired"
                                                                   : "unknown",
                            static_cast<int>(SDL_GetGamepadType(gp)));
                char* m = SDL_GetGamepadMapping(gp);
                std::printf("  mapping  %s\n", m ? m : "(none)");
                SDL_free(m);
            }
        } else {
            SDL_OpenJoystick(id);
        }
    };
    const Uint64 end = SDL_GetTicks() + static_cast<Uint64>(seconds) * 1000;
    while (SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_JOYSTICK_ADDED: describe(e.jdevice.which); break;
                case SDL_EVENT_JOYSTICK_REMOVED:
                    std::printf("pad %u gone\n", static_cast<unsigned>(e.jdevice.which));
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    std::printf("pad %u pressed %s\n", static_cast<unsigned>(e.gbutton.which),
                                SDL_GetGamepadStringForButton(
                                    static_cast<SDL_GamepadButton>(e.gbutton.button)));
                    break;
                case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
                    if (!SDL_IsGamepad(e.jbutton.which))
                        std::printf("pad %u raw button %d\n", static_cast<unsigned>(e.jbutton.which),
                                    e.jbutton.button);
                    break;
                default: break;
            }
            std::fflush(stdout);
        }
        SDL_Delay(10);
    }
    SDL_Quit();
    return 0;
}

}  // namespace players
