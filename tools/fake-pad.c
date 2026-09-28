// A pretend controller, for testing what reaches a game with nobody holding one.
//
// Creates an Xbox 360-style pad through /dev/uinput, which every SDL on the
// machine sees as a real controller, then taps a button on a timer. Run on the
// A9 while a game is up to prove the whole path: real pad, the console's SDL,
// players.h, the player's virtual controller (vpad.h), the emulator.
//
//   gcc -O2 -o fake-pad tools/fake-pad.c
//   ./fake-pad WAIT TAPS EVERY [east|south|start]
//
// Waits WAIT seconds, taps the button TAPS times, EVERY seconds apart, and
// stays a controller for five more seconds before it goes. `east` (Xbox B) is
// the default because that position is A on a Switch.

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void emit(int fd, int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd, &ev, sizeof ev) < 0) perror("write");
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s WAIT TAPS EVERY [east|south|start]\n", argv[0]);
        return 2;
    }
    const double wait = atof(argv[1]);
    const int taps = atoi(argv[2]);
    const double every = atof(argv[3]);
    int code = BTN_EAST;
    if (argc > 4 && strcmp(argv[4], "south") == 0) code = BTN_SOUTH;
    if (argc > 4 && strcmp(argv[4], "start") == 0) code = BTN_START;

    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    const int keys[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR,
                        BTN_SELECT, BTN_START, BTN_MODE, BTN_THUMBL, BTN_THUMBR};
    const int axes[] = {ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ, ABS_HAT0X, ABS_HAT0Y};
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; ++i) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; ++i) ioctl(fd, UI_SET_ABSBIT, axes[i]);

    struct uinput_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x045e;
    setup.id.product = 0x028e;
    setup.id.version = 0x0114;
    snprintf(setup.name, sizeof setup.name, "Microsoft X-Box 360 pad");
    ioctl(fd, UI_DEV_SETUP, &setup);
    for (size_t i = 0; i < sizeof axes / sizeof axes[0]; ++i) {
        struct uinput_abs_setup a;
        memset(&a, 0, sizeof a);
        a.code = axes[i];
        const int hat = axes[i] == ABS_HAT0X || axes[i] == ABS_HAT0Y;
        const int trigger = axes[i] == ABS_Z || axes[i] == ABS_RZ;
        a.absinfo.minimum = hat ? -1 : trigger ? 0 : -32768;
        a.absinfo.maximum = hat ? 1 : trigger ? 255 : 32767;
        ioctl(fd, UI_ABS_SETUP, &a);
    }
    if (ioctl(fd, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }
    printf("fake pad up\n");
    fflush(stdout);

    usleep((useconds_t)(wait * 1e6));
    for (int i = 0; i < taps; ++i) {
        emit(fd, EV_KEY, code, 1);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        usleep(120000);
        emit(fd, EV_KEY, code, 0);
        emit(fd, EV_SYN, SYN_REPORT, 0);
        printf("tap %d\n", i + 1);
        fflush(stdout);
        usleep((useconds_t)(every * 1e6));
    }
    sleep(5);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}
