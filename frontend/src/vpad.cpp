#include "vpad.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace cab::vpad {
namespace {

// pid.codes' open-source vendor ID and a product number nobody else ships, so
// no real controller can ever be mistaken for one of these, and the console's
// own SDL can be told to leave them alone (see main.cpp).
constexpr uint16_t kVendor = 0x1209;
constexpr uint16_t kProduct = 0xCAB0;
constexpr uint16_t kVersion = 0x0001;

// In key-code order, which is SDL's numbering: SOUTH is button 0.
struct Key {
    SDL_GamepadButton button;
    int code;
};
constexpr Key kKeys[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, BTN_SOUTH},             // 0x130
    {SDL_GAMEPAD_BUTTON_EAST, BTN_EAST},               // 0x131
    {SDL_GAMEPAD_BUTTON_NORTH, BTN_NORTH},             // 0x133
    {SDL_GAMEPAD_BUTTON_WEST, BTN_WEST},               // 0x134
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, BTN_TL},        // 0x136
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, BTN_TR},       // 0x137
    {SDL_GAMEPAD_BUTTON_BACK, BTN_SELECT},             // 0x13a
    {SDL_GAMEPAD_BUTTON_START, BTN_START},             // 0x13b
    {SDL_GAMEPAD_BUTTON_GUIDE, BTN_MODE},              // 0x13c
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, BTN_THUMBL},       // 0x13d
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, BTN_THUMBR},      // 0x13e
};

// Likewise in code order: ABS_X is axis 0. The hat is not an axis to SDL.
struct Abs {
    SDL_GamepadAxis axis;
    int code;
    int min;
};
constexpr Abs kAxes[] = {
    {SDL_GAMEPAD_AXIS_LEFTX, ABS_X, -32768},
    {SDL_GAMEPAD_AXIS_LEFTY, ABS_Y, -32768},
    {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, ABS_Z, 0},
    {SDL_GAMEPAD_AXIS_RIGHTX, ABS_RX, -32768},
    {SDL_GAMEPAD_AXIS_RIGHTY, ABS_RY, -32768},
    {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, ABS_RZ, 0},
};

int gFd[kMaxPlayers] = {-1, -1, -1, -1};
int gCount = 0;
// The d-pad is four buttons to SDL and one hat to evdev, so each pad's four
// are remembered to work out the hat.
bool gDpad[kMaxPlayers][4] = {};

void emit(int fd, int type, int code, int value) {
    input_event ev{};
    ev.type = static_cast<uint16_t>(type);
    ev.code = static_cast<uint16_t>(code);
    ev.value = value;
    if (::write(fd, &ev, sizeof ev) < 0) {
        // Nothing useful to do mid-game; a pad that stopped is reported by
        // the emulator not moving, and open() already said whether it worked.
    }
}

void sync(int fd) { emit(fd, EV_SYN, SYN_REPORT, 0); }

int create(int index) {
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0 && ioctl(fd, UI_SET_EVBIT, EV_ABS) == 0;
    for (const Key& k : kKeys) ok = ok && ioctl(fd, UI_SET_KEYBIT, k.code) == 0;
    for (const Abs& a : kAxes) ok = ok && ioctl(fd, UI_SET_ABSBIT, a.code) == 0;
    ok = ok && ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X) == 0 && ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y) == 0;

    uinput_setup setup{};
    setup.id.bustype = BUS_USB;
    setup.id.vendor = kVendor;
    setup.id.product = kProduct;
    setup.id.version = kVersion;
    std::snprintf(setup.name, sizeof setup.name, "CabinetOS player %d", index + 1);
    ok = ok && ioctl(fd, UI_DEV_SETUP, &setup) == 0;

    auto absSetup = [&](int code, int min, int max) {
        uinput_abs_setup a{};
        a.code = static_cast<uint16_t>(code);
        a.absinfo.minimum = min;
        a.absinfo.maximum = max;
        return ioctl(fd, UI_ABS_SETUP, &a) == 0;
    };
    for (const Abs& a : kAxes) ok = ok && absSetup(a.code, a.min, 32767);
    ok = ok && absSetup(ABS_HAT0X, -1, 1) && absSetup(ABS_HAT0Y, -1, 1);
    ok = ok && ioctl(fd, UI_DEV_CREATE) == 0;
    if (!ok) {
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace

bool open(int players) {
    close();
    if (players < 1) players = 1;
    if (players > kMaxPlayers) players = kMaxPlayers;
    for (int i = 0; i < players; ++i) {
        gFd[i] = create(i);
        if (gFd[i] < 0) {
            std::fprintf(stderr, "[vpad] could not create player %d's controller: %s\n", i + 1,
                         std::strerror(errno));
            close();
            return false;
        }
        ++gCount;
    }
    std::memset(gDpad, 0, sizeof gDpad);
    std::fprintf(stderr, "[vpad] %d virtual controller(s)\n", gCount);
    return true;
}

void close() {
    for (int& fd : gFd) {
        if (fd < 0) continue;
        ioctl(fd, UI_DEV_DESTROY);
        ::close(fd);
        fd = -1;
    }
    gCount = 0;
}

int count() { return gCount; }

void button(int player, SDL_GamepadButton b, bool down) {
    if (player < 0 || player >= gCount) return;
    const int fd = gFd[player];
    const int dpad = b == SDL_GAMEPAD_BUTTON_DPAD_UP      ? 0
                     : b == SDL_GAMEPAD_BUTTON_DPAD_DOWN  ? 1
                     : b == SDL_GAMEPAD_BUTTON_DPAD_LEFT  ? 2
                     : b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 3
                                                          : -1;
    if (dpad >= 0) {
        bool* d = gDpad[player];
        d[dpad] = down;
        emit(fd, EV_ABS, ABS_HAT0Y, d[0] == d[1] ? 0 : d[0] ? -1 : 1);
        emit(fd, EV_ABS, ABS_HAT0X, d[2] == d[3] ? 0 : d[2] ? -1 : 1);
        sync(fd);
        return;
    }
    for (const Key& k : kKeys) {
        if (k.button != b) continue;
        emit(fd, EV_KEY, k.code, down ? 1 : 0);
        sync(fd);
        return;
    }
}

void axis(int player, SDL_GamepadAxis a, int16_t value) {
    if (player < 0 || player >= gCount) return;
    for (const Abs& x : kAxes) {
        if (x.axis != a) continue;
        emit(gFd[player], EV_ABS, x.code, value);
        sync(gFd[player]);
        return;
    }
}

void releaseAll() {
    for (int p = 0; p < gCount; ++p) {
        const int fd = gFd[p];
        for (const Key& k : kKeys) emit(fd, EV_KEY, k.code, 0);
        for (const Abs& x : kAxes) emit(fd, EV_ABS, x.code, 0);
        emit(fd, EV_ABS, ABS_HAT0X, 0);
        emit(fd, EV_ABS, ABS_HAT0Y, 0);
        sync(fd);
        std::memset(gDpad[p], 0, sizeof gDpad[p]);
    }
}

std::string edenGuid() {
    // SDL's GUID for a USB evdev device: bus, the name's CRC, vendor, 0,
    // product, 0, version, then two driver bytes, each 16-bit field little
    // endian. Eden clears the CRC, so it is written as zero here.
    const uint16_t words[8] = {0x0003, 0, kVendor, 0, kProduct, 0, kVersion, 0};
    char out[33];
    for (int i = 0; i < 8; ++i)
        std::snprintf(out + i * 4, 5, "%02x%02x", words[i] & 0xFF, words[i] >> 8);
    return out;
}

int buttonIndex(SDL_GamepadButton b) {
    for (size_t i = 0; i < sizeof kKeys / sizeof kKeys[0]; ++i)
        if (kKeys[i].button == b) return static_cast<int>(i);
    return -1;
}

int axisIndex(SDL_GamepadAxis a) {
    for (size_t i = 0; i < sizeof kAxes / sizeof kAxes[0]; ++i)
        if (kAxes[i].axis == a) return static_cast<int>(i);
    return -1;
}

}  // namespace cab::vpad
