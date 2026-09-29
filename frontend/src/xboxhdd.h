// An Xbox hard drive, as xemu keeps it: a qcow2 file holding the Xbox's own
// FATX partitions. The console reads and writes the saves on it, and nothing
// else, while xemu is not running.
//
// WHY THE CONSOLE TOUCHES THE DRIVE AT ALL. An Xbox game saves to its hard
// drive, E:\UDATA\<title ID>\ (and a few also to E:\TDATA\<title ID>\), and
// xemu keeps that drive as one image file. RomM stores a file per game, and
// every other system here sends a zip of the game's own save folders
// (dirsave.h). So the drive is a go-between: before a game the person's save
// folders are written onto it, and after it they are read back off and zipped.
// The zip holds `UDATA/...` and `TDATA/...` exactly as they sit on E:, so it
// can be copied onto any xemu drive without CabinetOS. MMagTech, 2026-09-29;
// issue #172.
//
// WHY THE DRIVE CAN BE THROWN AWAY. It sits in the game's folder in the cache
// beside the disc image and goes when the game is evicted. Everything on it
// that matters is in the save zip; the rest is the game's own scratch space
// (the X, Y and Z cache partitions), which it refills as it plays.
//
// OUR OWN CODE, NOT A LIBRARY. mborgerson/fatx (by xemu's author) reads and
// writes FATX, but it is GPL and this console is MIT; linking it is a licence
// decision nobody has made. The format facts below were checked against it
// (libfatx/fatx.c, fatx_fat.c, fatx_internal.h) and against the drive xemu
// publishes, and the code is this file's.
//
// WHAT IS SUPPORTED, SAID PLAINLY: qcow2 version 2 or 3, zlib compression,
// no backing file, no encryption, no snapshots, no extended L2 entries. That
// is every drive xemu's own image and xemu itself produce while nobody takes
// a snapshot, and this console never does (save states are off for Xbox). A
// drive outside that is refused rather than guessed at. Rewriting a cluster
// that was compressed leaks its old bytes (the file grows a little); that is
// harmless to qemu and is what keeps the refcount handling to allocation only.

#pragma once

#include <string>

namespace cab::xboxhdd {

// Replaces E:\UDATA and E:\TDATA on the drive with what is under `root`
// (`root/UDATA/...`, `root/TDATA/...`). Either folder missing under `root`
// leaves an empty one on the drive. False with a reason, and the drive may
// then be half-written: the caller throws it away and starts from a blank one.
bool writeSaves(const std::string& drive, const std::string& root, std::string* err);

// Replaces `root/UDATA` and `root/TDATA` with what is on the drive.
bool readSaves(const std::string& drive, const std::string& root, std::string* err);

// Every file on E:\UDATA and E:\TDATA, one line each (`UDATA/4d530004/x.xsv
// 1234`), for tests and logs.
bool list(const std::string& drive, std::string* out, std::string* err);

}  // namespace cab::xboxhdd
