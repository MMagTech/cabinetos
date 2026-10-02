// Games on the fast cores, on a processor that has fast and slow ones (#163).
//
// THE KERNEL ALREADY KNOWS WHICH CORES ARE FAST, AND A GAME STILL LANDS ON THE
// SLOW ONES. The A9's Ryzen AI 9 HX 370 has 4 cores that reach 5.16 GHz and 8
// that reach 3.29. Its ranking reaches the scheduler (through AMD's HFI driver,
// which is why amd_pstate's own `prefcore` reads "disabled"), and steady work
// follows it: four busy loops ran 100% on the fast cores. A game does not. It
// works a few milliseconds and sleeps until the next frame, and on waking the
// scheduler takes whatever idle core is near. Measured 2026-10-01: Dolphin spent
// 96% of its CPU time on the slow cores, and the same game paced at its own
// rate cost 20% (GameCube) to 36% (Dreamcast) more work per frame there. Left
// alone a run lands anywhere in between, so the cost of a frame changes from
// one launch to the next, which #63 would read as a game struggling.
//
// SO WHILE A GAME RUNS INSIDE THE CONSOLE, the thread that loads it goes on the
// fast cores, and every thread the emulator starts from there inherits that.
// Threads the console started before (cover workers, downloads) keep every
// core, which keeps them out of the game's way. Back on Home, every core again.
//
// The separate emulator programs (Eden, RPCS3, Cemu, Xenia, xemu) are NOT
// placed: they spread over many threads, and whether eight is enough for them
// is a measurement nobody has made. proc's children start on every core.
//
// OFF THE A9: the fast cores are the ones whose top clock is near the highest,
// which every Linux processor reports (`cpuinfo_max_freq`). Intel's P and E
// cores are found the same way. Where all cores are alike, or fewer than four
// are fast, nothing is placed: squeezing a game onto two cores could cost more
// than the slow ones do.

#pragma once

namespace cpus {

// The calling thread, and every thread it starts from now on, on the fast
// cores. Finds them the first time and says what it found. Does nothing on a
// machine without enough of them. Safe to call twice.
void gameStart();

// The calling thread back on every core the console started with.
void gameEnd();

// For a child between fork() and exec(): every core the console started with,
// whatever the parent's thread was on. Only a system call, so it is safe there.
void childOnAllCores();

}  // namespace cpus
